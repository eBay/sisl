/*
 * file_monitor.hpp
 *
 *  Created on: Aug 19, 2017
 *      Author: maditya
 */

#ifndef FILE_MONITOR_HPP_
#define FILE_MONITOR_HPP_

#include <limits.h>
#include <event2/util.h>
#include <event2/event.h>
#include <string>
#include <functional>
#include <fstream>
#include <optional>

#ifdef __linux__
#include <sys/inotify.h>
#endif

#ifdef __APPLE__

#include <fcntl.h>  /* open() */
#include <unistd.h> /* close() */

#include <sys/types.h>
#include <sys/event.h>

typedef struct watchfile_kqueue {
    int kq;
    int filefd;
    struct event* event;
} watchfile_kqueue_t;

#endif

namespace sisl {

/* Monitors for file modification events */
class FileMonitor {
private:
    // event base passed from admin server
    struct event_base* m_base = nullptr;

    // filepath to monitor
    std::string m_filepath;

    // file contents
    std::optional< std::string > m_filecontents = "";

#if defined __linux__
    // inotify fd
    int m_inotify_fd;
    int m_wd;
    struct bufferevent* m_bev = nullptr;
#elif defined __APPLE__
    watchfile_kqueue_t m_wf;
    struct event* m_ewatch;
#else
    // retry timer is used for not linux platforms
    struct event* m_retry_timer = nullptr;
#endif

    // closure to be called when file modification is detected
    std::function< void(bool) > m_closure;

public:
    FileMonitor(struct event_base* base, const std::string filepath) : m_base(base), m_filepath(filepath) {
        /* read the contents of the file */
        m_filecontents = read_contents();

#ifdef __linux__
        m_inotify_fd = inotify_init();
        if (m_inotify_fd == -1) {
            PLOG(ERROR) << "inotify_init error";
            throw std::runtime_error("inotify_init failed!");
        }
        evutil_make_socket_nonblocking(m_inotify_fd);
#elif defined __APPLE__
        // struct kevent events_to_monitor[1];
        // struct kevent event_data[1];
        struct kevent ke;
        /* Open a kernel queue. */
        if ((m_wf.kq = kqueue()) < 0) {
            PLOG(ERROR) << "Could not open kernel queue";
            throw std::runtime_error("inotify_init failed!");
        }
        m_wf.filefd = open(m_filepath.c_str(), O_RDONLY | O_EVTONLY);
        if (m_wf.filefd <= 0) {
            PLOG(ERROR) << "The filepath = '" << m_filepath << "' could not be opened for monitoring using O_EVTONLY";
            throw std::runtime_error("inotify_init failed!");
        }

        EV_SET(&ke, m_wf.filefd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
               NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND /* | NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME | NOTE_REVOKE*/, 0,
               NULL);
        if (kevent(m_wf.kq, &ke, 1, NULL, 0, NULL) == -1) {
            PLOG(ERROR) << "kevent failed";
            throw std::runtime_error("inotify_init failed!");
        }

#else
        m_retry_timer = evtimer_new(m_base, FileMonitor::timer_cb, this);
        if (!m_retry_timer) { throw std::runtime_error("evtimer_new() failed!"); }
#endif
    }

    void register_listener(const std::function< void(bool) >& closure) {
        m_closure = std::move(closure);
#ifdef __linux__
        m_wd = inotify_add_watch(m_inotify_fd, m_filepath.c_str(), IN_ALL_EVENTS);
        if (m_wd == -1) {
            PLOG(WARNING) << "inotify_add_watch('" << m_filepath << "')";
            return;
        }

        m_bev = bufferevent_socket_new(m_base, m_inotify_fd, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_setcb(m_bev, readcb, NULL, NULL, this);
        int error = bufferevent_enable(m_bev, EV_READ);
        if (error != 0) {
            PLOG(ERROR) << "inotify_fd event_add failed! filepath = " << m_filepath;
            return;
        }
#elif defined __APPLE__
        m_ewatch = event_new(m_base, m_wf.kq, EV_READ | EV_ET | EV_PERSIST /*flags*/, apple_filewatch_cb, this);
        if (event_add(m_ewatch, NULL) == -1) {
            PLOG(WARNING) << "event_add failed! filepath = " << m_filepath;
            throw std::runtime_error("register_listener failed!");
        }
#else
        struct timeval tv = {10, 0};
        int error = evtimer_add(m_retry_timer, &tv);
        if (error != 0) {
            PLOG(ERROR) << "evtimer_add failed!";
            return;
        }
#endif
    }

    void unregister_listener() {
#ifdef __linux__
        inotify_rm_watch(m_inotify_fd, m_wd);
        if (m_bev != nullptr) { bufferevent_free(m_bev); }
#elif defined __APPLE__
        event_free(m_ewatch);
#else

#endif
    }

    void on_modified_event() {
        auto buffer = read_contents();
        if (!buffer.has_value()) { m_closure(true); }

        if (m_filecontents == buffer) {
            CVLOG(VMODULE_ADMIN, 1) << "File contents have not changed: " << m_filepath;
        } else {
            CVLOG(VMODULE_ADMIN, 1) << "File contents have changed: " << m_filepath;
            m_filecontents = std::move(buffer);
            // notify file modification event
            m_closure(false);
        }
    }

    std::optional< std::string > read_contents() {
        std::ifstream ifs(m_filepath);
        if (ifs.fail()) {
            PLOG(INFO) << "File does not exist: " << m_filepath;
            return {};
        }

        ifs.seekg(0, std::ios::end);
        auto size = ifs.tellg();
        if (size > 1024 * 1024 * 1024) {
            LOG(WARNING) << "File size larger than 1MB. Ignoring the file change event for: " << m_filepath;
            return m_filecontents;
        }

        std::string buffer(size, ' ');
        ifs.seekg(0);
        ifs.read(&buffer[0], size);
        return buffer;
    }

#ifdef __linux__
    static void readcb(struct bufferevent* bev, void* args) {
        auto pThis = (FileMonitor*)args;
        char buf[4096];
        size_t numRead = bufferevent_read(bev, buf, sizeof(buf));
        char* p;
        bool modified = false, ignored = false;
        for (p = buf; p < buf + numRead;) {
            struct inotify_event* event = (struct inotify_event*)p;
            log_inotify_event(event, pThis->m_filepath);
            if ((event->mask & IN_MOVE_SELF) || (event->mask & IN_MODIFY) || (event->mask & IN_CREATE) ||
                (event->mask & IN_DELETE_SELF) || (event->mask & IN_UNMOUNT)) {
                modified = true;
            }
            if (event->mask & IN_IGNORED) { ignored = true; }
            p += sizeof(struct inotify_event) + event->len;
        }

        if (modified) { pThis->on_modified_event(); }

        // if watch is removed due to file delete or unmount, add it back
        if (ignored) pThis->register_listener(pThis->m_closure);
    }
#elif defined __APPLE__
    static void apple_filewatch_cb(evutil_socket_t fd, short flags, void* arg) {
        FileMonitor* const pThis = (FileMonitor* const)arg;

        struct kevent ke;
        struct timespec const ts = {.tv_sec = 0, .tv_nsec = 0};

        if (kevent(pThis->m_wf.kq, NULL, 0, &ke, 1, &ts) == -1) {
            PLOG(WARNING) << "Failed to fetch kevent. filepath = " << pThis->m_filepath;
            return;
        }
        CVLOG(VMODULE_ADMIN, 1) << "Received file watch kevent on file " << pThis->m_filepath;
        pThis->on_modified_event();
    }
#else
    static void timer_cb(evutil_socket_t fd, short what, void* arg) {
        auto pThis = (FileMonitor*)arg;
        pThis->m_closure();
        pThis->register_listener(pThis->m_closure);
    }
#endif

#ifdef __linux__
    static void log_inotify_event(struct inotify_event* i, std::string& filepath) {
        std::stringstream ss;
        ss << "filepath = " << filepath;
        ss << ", wd = " << i->wd;
        ss << ", mask = " << i->mask;

        if (i->cookie > 0) ss << ", cookie = " << i->cookie;
        if (i->mask & IN_ACCESS) ss << ", IN_ACCESS";
        if (i->mask & IN_ATTRIB) ss << ", IN_ATTRIB";
        if (i->mask & IN_CLOSE_NOWRITE) ss << ", IN_CLOSE_NOWRITE";
        if (i->mask & IN_CLOSE_WRITE) ss << ", IN_CLOSE_WRITE";
        if (i->mask & IN_CREATE) ss << ", IN_CREATE";
        if (i->mask & IN_DELETE) ss << ", IN_DELETE";
        if (i->mask & IN_DELETE_SELF) ss << ", IN_DELETE_SELF";
        if (i->mask & IN_IGNORED) ss << ", IN_IGNORED";
        if (i->mask & IN_ISDIR) ss << ", IN_ISDIR";
        if (i->mask & IN_MODIFY) ss << ", IN_MODIFY";
        if (i->mask & IN_MOVE_SELF) ss << ", IN_MOVE_SELF";
        if (i->mask & IN_MOVED_FROM) ss << ", IN_MOVED_FROM";
        if (i->mask & IN_MOVED_TO) ss << ", IN_MOVED_TO";
        if (i->mask & IN_OPEN) ss << ", IN_OPEN";
        if (i->mask & IN_Q_OVERFLOW) ss << ", IN_Q_OVERFLOW";
        if (i->mask & IN_UNMOUNT) ss << ", IN_UNMOUNT";

        if (i->len > 0) ss << ", filepath = " << i->name;

        CVLOG(VMODULE_ADMIN, 2) << "inotify event:: '" << ss.str() << "'";
    }
#endif
};

} // namespace sisl
#endif /* FILE_MONITOR_HPP_ */
