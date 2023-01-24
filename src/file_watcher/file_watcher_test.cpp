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
    std::shared_ptr< FileWatcher > file_watcher;
    virtual void SetUp() override {
        file_watcher = std::make_shared< FileWatcher >();
        EXPECT_TRUE(file_watcher->start());
    }

    virtual void TearDown() override { EXPECT_TRUE(file_watcher->stop()); }

    std::mutex file_change_lock;
    std::condition_variable file_change_cv;
};

TEST_F(FileWatcherTest, basic_watcher) {
    const auto file_path = fs::current_path() / "basic_test.txt";
    // remove if exists and then create a new file
    fs::remove(file_path);
    const std::string file_str{file_path.string()};
    std::ofstream file_of{file_str};
    bool is_deleted = true;
    bool cb_called = false;

    EXPECT_TRUE(file_watcher->register_listener(
        file_str, "basic_test_listener",
        [this, &is_deleted, &cb_called, &file_str](const std::string filepath, const bool deleted) {
            EXPECT_EQ(file_str, filepath);
            {
                std::lock_guard< std::mutex > lg(file_change_lock);
                is_deleted = deleted;
                cb_called = true;
            }
            file_change_cv.notify_one();
        }));

    // edit the file
    file_of << "Hello World!";
    file_of.flush();
    {
        auto lk = std::unique_lock< std::mutex >(file_change_lock);
        EXPECT_TRUE(file_change_cv.wait_for(lk, std::chrono::milliseconds(500), [&cb_called]() { return cb_called; }));
        EXPECT_FALSE(is_deleted);
        cb_called = false; // set it false for the next iteration of the test
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
