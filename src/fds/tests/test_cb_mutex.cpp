
#include <iostream>
#include <thread>
#include <vector>
#include <sds_logging/logging.h>
#include <sds_options/options.h>
#include <gtest/gtest.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <folly/SharedMutex.h>
#pragma GCC diagnostic pop

#include "callback_mutex.hpp"
#include "utils.hpp"

SDS_LOGGING_INIT(test_cb_mutex)

static uint64_t g_prev_val{0};
static uint64_t g_cur_val{1};

template < typename MutexImpl >
class CBMutexTest : public testing::Test {
protected:
    sisl::CallbackMutex< MutexImpl > m_cb_mtx;

protected:
    void thread_unique_fn(uint64_t count_per_thread) {
        uint64_t count{0};
        for (uint64_t i{0}; i < count_per_thread; ++i) {
            sisl::CBUniqueLock< MutexImpl > h(m_cb_mtx, [&count]() {
                assert((g_prev_val + 1) == g_cur_val);
                g_prev_val = g_cur_val++;
                ++count;
            });
        }

        LOGINFO("Thread attempted {} of exclusive callbacks", count);
    }

    template < typename I = MutexImpl >
    typename std::enable_if< sisl::CallbackMutex< I >::shared_mode_supported, void >::type
    thread_shared_fn(uint64_t count_per_thread) {
        uint64_t count{0};
        for (uint64_t i{0}; i < count_per_thread; ++i) {
            sisl::CBSharedLock< MutexImpl > h(m_cb_mtx, [&count]() {
                assert((g_prev_val + 1) == g_cur_val);
                ++count;
            });
        }

        LOGINFO("Thread attempted {} of shared callbacks", count);
    }

    template < typename I = MutexImpl >
    typename std::enable_if< !sisl::CallbackMutex< I >::shared_mode_supported, void >::type
    thread_shared_fn(uint64_t count_per_thread) {
        assert(0);
    }

    void run_lock_unlock() {
        auto num_threads = SDS_OPTIONS["num_threads"].as< uint32_t >();
        auto num_iters = sisl::round_up(SDS_OPTIONS["num_iters"].as< uint64_t >(), num_threads);

        uint32_t unique_threads{num_threads};
        uint32_t shared_threads{0};
        if (sisl::CallbackMutex< MutexImpl >::shared_mode_supported) {
            unique_threads = std::max(1u, num_threads / 4);
            shared_threads = std::max(1u, num_threads - unique_threads);
        }

        std::vector< std::thread* > threads;
        for (uint32_t i{0}; i < unique_threads; ++i) {
            threads.push_back(new std::thread(bind_this(CBMutexTest::thread_unique_fn, 1), num_iters / num_threads));
        }

        for (uint32_t i{0}; i < shared_threads; ++i) {
            threads.push_back(new std::thread(bind_this(CBMutexTest::thread_shared_fn<>, 1), num_iters / num_threads));
        }
        for (auto t : threads) {
            t->join();
            delete (t);
        }
    }
};

using testing::Types;
typedef Types< std::mutex, std::shared_mutex, folly::SharedMutex > Implementations;
TYPED_TEST_SUITE(CBMutexTest, Implementations);

TYPED_TEST(CBMutexTest, LockUnlockTest) { this->run_lock_unlock(); }

SDS_OPTIONS_ENABLE(logging, test_cb_mutex)
SDS_OPTION_GROUP(test_cb_mutex,
                 (num_threads, "", "num_threads", "number of threads",
                  ::cxxopts::value< uint32_t >()->default_value("8"), "number"),
                 (num_iters, "", "num_iters", "number of iterations",
                  ::cxxopts::value< uint64_t >()->default_value("10000"), "number"))

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SDS_OPTIONS_LOAD(argc, argv, logging, test_cb_mutex)
    sds_logging::SetLogger("test_cb_mutex");
    spdlog::set_pattern("[%D %T%z] [%^%L%$] [%t] %v");

    auto ret = RUN_ALL_TESTS();
    return ret;
}
