#include <filesystem>
#include <limits.h>
#include <fstream>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <sys/inotify.h>

#include "sisl/file_watcher/file_watcher.hpp"
#include "sisl/utility/thread_factory.hpp"

namespace sisl {
namespace fs = std::filesystem;

bool FileWatcher::start() {
    m_inotify_fd = inotify_init1(IN_NONBLOCK);
    // create an fd for accessing inotify
    if (m_inotify_fd == -1) {
        LOGERROR("inotify_init failed!, errno: {}", errno);
        return false;
    }

    // init pipe which can be added to the poll event later
    if (pipe(m_pipefd) < 0) {
        LOGERROR("pipe creation failed!, errno: {}", errno);
        return true;
    }
    m_fw_thread = sisl::make_unique_thread("filewatcher", &FileWatcher::run, this);
    return true;
}

void FileWatcher::run() {
    // prepare pollfds for inotify_fd and pipe_fd
    pollfd fds[2];
    fds[0].fd = m_pipefd[0];
    fds[0].events = POLLIN;

    fds[1].fd = m_inotify_fd;
    fds[1].events = POLLIN;

    nfds_t nfds = 2;
    int poll_num;
    // start the poll loop. This will block the thread
    while (true) {
        poll_num = poll(fds, nfds, -1);
        if (poll_num == -1) {
            if (errno == EINTR) { continue; }
            LOGERROR("file watcher poll command failed!, errno: {}", errno);
            break;
        }

        if (poll_num > 0) {
            if (fds[0].revents & POLLIN) {
                LOGINFO("file watcher pipe event, shutdown signalled");
                break;
            }
            if (fds[1].revents & POLLIN) { handle_events(); }
        }
    }

    close(m_inotify_fd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);
}

bool FileWatcher::register_listener(const std::string& file_path, const std::string& listener_id,
                                    const file_event_cb_t& file_event_handler) {
    if (!check_file_size(file_path)) { return false; }
    {
        auto lk = std::unique_lock< std::mutex >(m_files_lock);
        if (const auto it{m_files.find(file_path)}; it != m_files.end()) {
            auto& file_info = it->second;
            file_info.m_handlers.emplace(listener_id, file_event_handler);
            LOGDEBUG("File path {} exists, adding the handler cb for the listener {}", file_path, listener_id);
            return true;
        }
    }

    FileInfo file_info;
    file_info.m_filepath = file_path;
    if (!get_file_contents(file_path, file_info.m_filecontents)) {
        LOGERROR("could not read contents from the file: [{}]", file_path);
        return false;
    }
    file_info.m_handlers.emplace(listener_id, file_event_handler);

    file_info.m_wd = inotify_add_watch(m_inotify_fd, file_path.c_str(), IN_ALL_EVENTS);
    if (file_info.m_wd == -1) {
        LOGWARN("inotify_add_watch({}) error, errno: {}", file_path, errno);
        return false;
    }

    {
        auto lk = std::unique_lock< std::mutex >(m_files_lock);
        m_files.emplace(file_path, file_info);
    }

    return true;
}

bool FileWatcher::unregister_listener(const std::string& file_path, const std::string& listener_id) {
    auto lk = std::unique_lock< std::mutex >(m_files_lock);
    FileInfo file_info;
    if (const auto it{m_files.find(file_path)}; it != m_files.end()) {
        file_info = it->second;
    } else {
        LOGWARN("file path {}, listener id {} not found!", file_path, listener_id);
        return false;
    }

    file_info.m_handlers.erase(listener_id);
    if (file_info.m_handlers.empty()) {
        if (!remove_watcher(file_info)) {
            LOGDEBUG("inotify rm failed for file path {}, listener id {} errno: {}", file_path, listener_id, errno);
            return false;
        }
    }
    return true;
}

bool FileWatcher::remove_watcher(FileInfo& file_info) {
    bool success = true;
    if (auto err = inotify_rm_watch(m_inotify_fd, file_info.m_wd); err != 0) { success = false; }
    // remove the file from the map regardless of the inotify_rm_watch result
    m_files.erase(file_info.m_filepath);
    return success;
}

bool FileWatcher::stop() {
    // signal event loop to break and wait for the thread to join
    // event value does not matter, this is just generating an event at the read end of the pipe
    LOGDEBUG("Stopping file watcher event loop.");
    int event = 1;
    int ret;
    do {
        ret = write(m_pipefd[1], &event, sizeof(int));
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        LOGERROR("Write to pipe during file watcher shutdown failed, errno: {}", errno);
        return false;
    }

    LOGDEBUG("Waiting for file watcher thread to join..");
    if (m_fw_thread && m_fw_thread->joinable()) {
        try {
            m_fw_thread->join();
        } catch (std::exception& e) {
            LOGERROR("file watcher thread join error: {}", e.what());
            return false;
        }
    }
    LOGINFO("file watcher stopped.");
    return true;
}

void FileWatcher::handle_events() {
    // some constants used to extract bytes from the event
    static const uint16_t MAX_EVENTS = 1024;
    static const size_t EVENT_SIZE = sizeof(inotify_event) + NAME_MAX + 1;
    static const size_t BUF_LEN = MAX_EVENTS * EVENT_SIZE;

    /* the buffer used for reading from the inotify file descriptor should have
     * the same alignment as struct inotify_event for better performance
     */
    char buf[BUF_LEN] __attribute__((aligned(__alignof__(inotify_event))));
    const inotify_event* event;
    ssize_t len;
    for (;;) {
        len = read(m_inotify_fd, buf, BUF_LEN);
        if (len == -1 && errno != EAGAIN) {
            LOGERROR("read failed, errno: {}", errno);
            break;
        }
        if (len <= 0) break;
        for (char* ptr = buf; ptr < buf + len; ptr += sizeof(inotify_event) + event->len) {
            event = (const struct inotify_event*)ptr;

            // We set the is_deleted flag true in the cb when one of the events IN_MOVE_SELF, IN_DELETE_SELF,
            // IN_UNMOUNT occurs.
            LOGDEBUG("Handling event {} on wd {}", event->mask, event->wd);

            if ((event->mask & IN_MOVE_SELF) || (event->mask & IN_DELETE_SELF) || (event->mask & IN_UNMOUNT)) {
                on_modified_event(event->wd, true);
            }
            //
            if ((event->mask & IN_CLOSE_WRITE) || (event->mask & IN_ATTRIB)) { on_modified_event(event->wd, false); }

            // we on purpose skip IN_MODIFY as it will trigger on every single modification of files,
            // the aggregated one IN_CLOSE_WRITE when file was closed makes more sense.

            // if the watch is removed due to file deletion or fs unmount (mask IN_IGNORED),
            // the user is expected to re-register callback afer the file is up as re-registering
            // here might fail if the underlying file is not recreated before the call.
        }
    }
}

void FileWatcher::on_modified_event(const int wd, const bool is_deleted) {
    FileInfo file_info;
    get_fileinfo(wd, file_info);
    LOGDEBUG("on_modified_event, wd={}, is_deleted={}", wd, is_deleted);
    if (is_deleted) {
        // There is a corner case (very unlikely) where a new listener
        // regestered for this filepath  after the current delete event was triggered.
        {
            auto lk = std::unique_lock< std::mutex >(m_files_lock);
            remove_watcher(file_info);
        }
        for (const auto& [id, handler] : file_info.m_handlers) {
            handler(file_info.m_filepath, true);
        }
        return;
    }

    if (!check_file_size(file_info.m_filepath)) { return; }

    const auto cur_buffer = file_info.m_filecontents;
    if (!get_file_contents(file_info.m_filepath, file_info.m_filecontents)) {
        LOGWARN("Could not read contents from the file: {}", file_info.m_filepath);
        return;
    }
    if (file_info.m_filecontents == cur_buffer) {
        LOGDEBUG("File contents have not changed: {}", file_info.m_filepath);
    } else {
        LOGDEBUG("File contents have changed: {}, from {} to {}", file_info.m_filepath, cur_buffer, file_info.m_filecontents);
        // notify file modification event
        for (const auto& [id, handler] : file_info.m_handlers) {
            handler(file_info.m_filepath, false);
        }
	set_fileinfo_content(wd, file_info.m_filecontents);
    }
}

bool FileWatcher::check_file_size(const std::string& file_path) {
    // currently supports file size < 1 Mb
    try {
        const auto sz = fs::file_size(fs::path{file_path});
        if (sz > 1024u * 1024 * 1024) {
            LOGERROR("File: [{}] size: [{}] larger than 1MB not supported", file_path, sz);
            return false;
        }
    } catch (fs::filesystem_error& e) {
        LOGERROR("could not get the file size for: {}, what: {}", file_path, e.what());
        return false;
    }

    return true;
}

bool FileWatcher::get_file_contents(const std::string& file_name, std::string& contents) {
    try {
        std::ifstream f(file_name);
        std::string buffer(std::istreambuf_iterator< char >{f}, std::istreambuf_iterator< char >{});
        contents = buffer;
        return true;
    } catch (...) {}
    return false;
}

void FileWatcher::get_fileinfo(const int wd, FileInfo& file_info) const {
    auto lk = std::unique_lock< std::mutex >(m_files_lock);
    for (const auto& [file_path, file] : m_files) {
        if (file.m_wd == wd) {
            file_info = file;
            return;
        }
    }
    LOGWARN("wd {} not found!", wd);
}

void FileWatcher::set_fileinfo_content(const int wd, const std::string& content) {
    auto lk = std::unique_lock< std::mutex >(m_files_lock);
    for (auto& [file_path, file] : m_files) {
        if (file.m_wd == wd) {
            file.m_filecontents = content;
            return;
        }
    }
    LOGWARN("wd {} not found!", wd);
}
} // namespace sisl
