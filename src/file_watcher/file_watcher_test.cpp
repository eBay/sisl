#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sisl/file_watcher/file_watcher.hpp"
#include <sisl/options/options.h>

SISL_LOGGING_INIT(test_file_watcher)
SISL_OPTIONS_ENABLE(logging)

namespace sisl::testing {
namespace fs = std::filesystem;
using namespace ::testing;

class FileWatcherTest : public ::testing::Test {
public:
    virtual void SetUp() override {
        m_file_change_params.file_watcher = std::make_shared< FileWatcher >();
        EXPECT_TRUE(m_file_change_params.file_watcher->start());
    }

    virtual void TearDown() override {
        EXPECT_TRUE(m_file_change_params.file_watcher->stop());
        std::remove(m_file_change_params.file_str.c_str());
    }

    struct FileChangeParams {
        std::shared_ptr< FileWatcher > file_watcher;
        std::string file_str;
        std::mutex file_change_lock;
        std::condition_variable file_change_cv;
        bool is_deleted;
        int cb_call_count;
    };
    FileChangeParams m_file_change_params;
};

void monitor_file_changes(FileWatcherTest::FileChangeParams& file_change_params, const std::string& listener) {
    EXPECT_TRUE(file_change_params.file_watcher->register_listener(
        file_change_params.file_str, listener,
        [&file_change_params, listener](const std::string filepath, const bool deleted) {
            EXPECT_EQ(file_change_params.file_str, filepath);
	    LOGWARN("CB called with deleted = {}", deleted);
            {
                std::lock_guard< std::mutex > lg(file_change_params.file_change_lock);
                file_change_params.is_deleted = deleted;
                file_change_params.cb_call_count--;
            }
            if (deleted) {
                std::ofstream file_of{file_change_params.file_str};
                monitor_file_changes(file_change_params, listener);
            }
            file_change_params.file_change_cv.notify_one();
        }));
}

TEST_F(FileWatcherTest, basic_watcher) {
    const auto file_path = fs::current_path() / "basic_test.txt";
    // remove if exists and then create a new file
    fs::remove(file_path);
    m_file_change_params.file_str = file_path.string();
    std::ofstream file_of{m_file_change_params.file_str};
    m_file_change_params.is_deleted = true;
    m_file_change_params.cb_call_count = 1;

    monitor_file_changes(m_file_change_params, "basic_listener");

    // edit the file
    file_of << "Hello World!";
    file_of.flush();
    file_of.close();
    {
        auto lk = std::unique_lock< std::mutex >(m_file_change_params.file_change_lock);
        EXPECT_TRUE(m_file_change_params.file_change_cv.wait_for(
            lk, std::chrono::milliseconds(1500), [this]() { return m_file_change_params.cb_call_count == 0; }));
        EXPECT_FALSE(m_file_change_params.is_deleted);
        m_file_change_params.cb_call_count = 1; // set it 1 for the next iteration of the test
    }
    // remove the file
    fs::remove(file_path);
    {
        auto lk = std::unique_lock< std::mutex >(m_file_change_params.file_change_lock);
        EXPECT_TRUE(m_file_change_params.file_change_cv.wait_for(
            lk, std::chrono::milliseconds(1500), [this]() { return m_file_change_params.cb_call_count == 0; }));
        EXPECT_TRUE(m_file_change_params.is_deleted);
        m_file_change_params.cb_call_count = 1; // set it 1 for the next iteration of the test
    }

    /* TODO fix this in CI.
    std::ofstream file_of1{m_file_change_params.file_str};
    file_of1 << "Hello World Again!";
    file_of1.flush();
    {
        auto lk = std::unique_lock< std::mutex >(m_file_change_params.file_change_lock);
        EXPECT_TRUE(m_file_change_params.file_change_cv.wait_for(
            lk, std::chrono::milliseconds(1500), [this]() { return m_file_change_params.cb_call_count == 0; }));
        EXPECT_FALSE(m_file_change_params.is_deleted);
    }
    */
}

TEST_F(FileWatcherTest, cert_watcher_simulation) {
    const auto file_path = fs::current_path() / "cert.crt";
    // remove if exists and then create a new file
    fs::remove(file_path);
    m_file_change_params.file_str = file_path.string();
    std::ofstream org_file_of{file_path.string(), std::ios::out | std::ios::trunc};
    org_file_of << "Good Morning!";
    org_file_of.flush();
    org_file_of.close();

    m_file_change_params.is_deleted = true;
    m_file_change_params.cb_call_count = 1;

    monitor_file_changes(m_file_change_params, "basic_listener");
    // open with trunc
    std::ofstream file_of{file_path.string(), std::ios::out | std::ios::trunc};
    // edit the file
    file_of << "Hello World!";
    file_of.flush();
    file_of.close();
    // 2nd open and close, no cb should be called
    file_of.open(file_path.string(), std::ios::app);
    file_of.close();
    // doing a chmod to the file
    auto ret = chmod(file_path.string().c_str(), 0777);
    LOGDEBUG("after CHMOD, ret={}", ret);

    // ensure the CB only called once.
    {
        auto lk = std::unique_lock< std::mutex >(m_file_change_params.file_change_lock);
        EXPECT_TRUE(m_file_change_params.file_change_cv.wait_for(
            lk, std::chrono::milliseconds(1500), [this]() { return m_file_change_params.cb_call_count == 0; }));
        EXPECT_FALSE(m_file_change_params.is_deleted);
        m_file_change_params.cb_call_count = 1; // set it 1 for the next iteration of the test
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOGDEBUG("BEFORE REMOVING FILE");
    // remove the file
    fs::remove(file_path);
    {
        auto lk = std::unique_lock< std::mutex >(m_file_change_params.file_change_lock);
        EXPECT_TRUE(m_file_change_params.file_change_cv.wait_for(
            lk, std::chrono::milliseconds(1500), [this]() { return m_file_change_params.cb_call_count == 0; }));
        EXPECT_TRUE(m_file_change_params.is_deleted);
        m_file_change_params.cb_call_count = 1; // set it 1 for the next iteration of the test
    }
}

TEST_F(FileWatcherTest, multiple_watchers) {
    const auto file_path = fs::current_path() / "basic_test.txt";
    // remove if exists and then create a new file
    fs::remove(file_path);
    m_file_change_params.file_str = file_path.string();
    std::ofstream file_of{m_file_change_params.file_str};
    m_file_change_params.is_deleted = true;
    m_file_change_params.cb_call_count = 2;

    monitor_file_changes(m_file_change_params, "basic_listener1");
    monitor_file_changes(m_file_change_params, "basic_listener2");

    // edit the file
    file_of << "Hello World!";
    file_of.flush();
    file_of.close();
    {
        auto lk = std::unique_lock< std::mutex >(m_file_change_params.file_change_lock);
        EXPECT_TRUE(m_file_change_params.file_change_cv.wait_for(
            lk, std::chrono::milliseconds(1500), [this]() { return m_file_change_params.cb_call_count == 0; }));
        EXPECT_FALSE(m_file_change_params.is_deleted);
    }
}

} // namespace sisl::testing

int main(int argc, char* argv[]) {
    ::testing::InitGoogleMock(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging);
    sisl::logging::SetLogger("test_file_watcher");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");
    return RUN_ALL_TESTS();
}
