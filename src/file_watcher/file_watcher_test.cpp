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
        bool cb_called;
    };
    FileChangeParams m_file_change_params;
};

void monitor_file_changes(FileWatcherTest::FileChangeParams& file_change_params) {
    EXPECT_TRUE(file_change_params.file_watcher->register_listener(
        file_change_params.file_str, "basic_test_listener",
        [&file_change_params](const std::string filepath, const bool deleted) {
            EXPECT_EQ(file_change_params.file_str, filepath);
            {
                std::lock_guard< std::mutex > lg(file_change_params.file_change_lock);
                file_change_params.is_deleted = deleted;
                file_change_params.cb_called = true;
            }
            if (deleted) {
                std::ofstream file_of{file_change_params.file_str};
                monitor_file_changes(file_change_params);
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
    m_file_change_params.cb_called = false;

    monitor_file_changes(m_file_change_params);

    // edit the file
    file_of << "Hello World!";
    file_of.flush();
    {
        auto lk = std::unique_lock< std::mutex >(m_file_change_params.file_change_lock);
        EXPECT_TRUE(m_file_change_params.file_change_cv.wait_for(lk, std::chrono::milliseconds(500),
                                                                 [this]() { return m_file_change_params.cb_called; }));
        EXPECT_FALSE(m_file_change_params.is_deleted);
        m_file_change_params.cb_called = false; // set it false for the next iteration of the test
    }

    // remove the file
    fs::remove(file_path);
    {
        auto lk = std::unique_lock< std::mutex >(m_file_change_params.file_change_lock);
        EXPECT_TRUE(m_file_change_params.file_change_cv.wait_for(lk, std::chrono::milliseconds(500),
                                                                 [this]() { return m_file_change_params.cb_called; }));
        EXPECT_TRUE(m_file_change_params.is_deleted);
        m_file_change_params.cb_called = false; // set it false for the next iteration of the test
    }

    std::ofstream file_of1{m_file_change_params.file_str};
    file_of1 << "Hello World Again!";
    file_of1.flush();
    {
        auto lk = std::unique_lock< std::mutex >(m_file_change_params.file_change_lock);
        EXPECT_TRUE(m_file_change_params.file_change_cv.wait_for(lk, std::chrono::milliseconds(500),
                                                                 [this]() { return m_file_change_params.cb_called; }));
        EXPECT_FALSE(m_file_change_params.is_deleted);
        m_file_change_params.cb_called = false; // set it false for the next iteration of the test
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
