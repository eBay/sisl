// This implementation of file watcher only works on Linux machines.
#pragma once

#include <functional>
#include <sisl/logging/logging.h>
#include <thread>

namespace sisl {

using file_event_cb_t = std::function< void(const std::string, const bool) >;

// structure to hold file contents and closures to be run
struct FileInfo {
    FileInfo() {}
    std::string m_filepath;
    // file contents
    std::string m_filecontents;
    // closures to be called when file modification is detected, one per listener
    std::map< std::string, file_event_cb_t > m_handlers;
    int m_wd;
};

class FileWatcher {
public:
    FileWatcher() = default;

    bool start();
    bool register_listener(const std::string& file_path, const std::string& listener_id,
                           const file_event_cb_t& file_event_handler);
    bool unregister_listener(const std::string& file_path, const std::string& listener_id);
    bool stop();

private:
    void run();
    void handle_events();
    void get_fileinfo(const int wd, FileInfo& file_info) const;
    void on_modified_event(const int wd, const bool is_deleted);
    static bool get_file_contents(const std::string& file_name, std::string& contents);
    static bool check_file_size(const std::string& file_path);

private:
    int m_inotify_fd;
    std::map< std::string, FileInfo > m_files;
    mutable std::mutex m_files_lock;
    std::unique_ptr< std::thread > m_fw_thread;
    // This is used to notify poll loop to exit
    int m_pipefd[2] = {-1, -1};
};

} // namespace sisl
