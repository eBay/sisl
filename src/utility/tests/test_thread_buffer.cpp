//
// Created by Kadayam, Hari on 12/21/18.
//

#include <chrono>
#include <cstdint>
#include <iostream>
#include <list>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "utility/thread_buffer.hpp"

THREAD_BUFFER_INIT
RCU_REGISTER_INIT

using namespace sisl;

struct MyList {
    std::list< uint64_t > m_list;
    void add(MyList& other) {
        other.m_list.sort();
        m_list.merge(other.m_list);
    }
};

static constexpr size_t INITIAL_THREADS{6};
static constexpr size_t ADDLN_THREADS{2};
static constexpr size_t ENTRIES_PER_THREAD{10000};
static constexpr size_t TOTAL_THREADS{INITIAL_THREADS + ADDLN_THREADS};
static constexpr size_t TOTAL_ENTRIES(ENTRIES_PER_THREAD * TOTAL_THREADS);

struct ThreadBufferTest : public testing::Test {
    ThreadBufferTest() {
        for (size_t i{0}; i < INITIAL_THREADS; ++i) {
            m_threads.emplace_back(write_thread, i, this);
        }
        m_scrapper_thread = std::thread(scrapper_thread, this);
    }
    ThreadBufferTest(const ThreadBufferTest&) = delete;
    ThreadBufferTest(ThreadBufferTest&&) noexcept = delete;
    ThreadBufferTest& operator=(const ThreadBufferTest&) = delete;
    ThreadBufferTest& operator=(ThreadBufferTest&&) noexcept = delete;

    virtual ~ThreadBufferTest() override {
        if (m_scrapper_thread.joinable()) m_scrapper_thread.join();

        for (auto& t : m_threads) {
            if (t.joinable()) t.join();
        }
    }

protected:
    std::mutex m_print_mutex;
    std::vector< std::thread > m_threads;
    std::thread m_scrapper_thread;
    ExitSafeThreadBuffer< MyList > m_buffer;

    void SetUp() override {}
    void TearDown() override {}

    static void write_thread(const uint32_t thread_index, ThreadBufferTest* const test) {
        static thread_local std::random_device rd{};
        static thread_local std::default_random_engine engine{rd()};
        std::uniform_int_distribution< uint64_t > sleep_time{0, (thread_index + 1) * 100 - 1};
        const uint64_t lhs_mask{static_cast< uint64_t >(thread_index) << 32};
        for (size_t i{0}; i < ENTRIES_PER_THREAD; ++i) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_time(engine)));
            const uint64_t val{lhs_mask | i}; // Make sure entry is unique across the thread by putting thread as lhs
            test->m_buffer->m_list.push_back(val);
        }
        {
            std::scoped_lock< std::mutex > lock{test->m_print_mutex};
            std::cout << "Thread " << thread_index << " done writing" << std::endl;
        }
    }

    static void scrapper_thread(ThreadBufferTest* const test) {
        uint64_t total_entries_recvd{0};
        MyList merge_list;

        do {
            std::this_thread::sleep_for(std::chrono::microseconds(50000));
            test->m_buffer.access_all_threads([&merge_list](MyList* const ml, 
                                                            const bool is_thread_running,
                                                            [[maybe_unused]] const bool is_last_thread) {
                // it is a race to access the the thread MyList from the write thread and scrapper thread at the same time
                if (!is_thread_running) { merge_list.add(*ml); }
                return !is_thread_running;
            });

            const auto before_size{merge_list.m_list.size()};
            merge_list.m_list.unique();
            ASSERT_EQ(before_size, merge_list.m_list.size());
            total_entries_recvd = merge_list.m_list.size();

            {
                std::scoped_lock< std::mutex > lock{test->m_print_mutex};
                std::cout << "Scrapped " << merge_list.m_list.size()
                          << " in this iteration. total_entries_recvd so far = " << total_entries_recvd << std::endl;
            }

            // Start any remaining new threads
            for (size_t i{test->m_threads.size()}; i < TOTAL_THREADS; ++i) {
                test->m_threads.emplace_back(write_thread, i, test);
                std::cout << "added thread " << i << std::endl;
            }
        } while (total_entries_recvd < TOTAL_ENTRIES);

        {
            std::scoped_lock< std::mutex > lock{test->m_print_mutex};
            std::cout << "Scrapped all entries, total_entries_recvd = " << total_entries_recvd << std::endl;
            ;
        }
        merge_list.m_list.sort();
        auto it{std::cbegin(merge_list.m_list)};
        for (size_t i{0}; i < test->m_threads.size(); ++i) {
            const uint64_t lhs_mask{static_cast< uint64_t >(i) << 32};
            for (size_t i2{0}; i2 < ENTRIES_PER_THREAD && it != std::cend(merge_list.m_list); ++i2, ++it) {
                const uint64_t expected{lhs_mask | i2};
                ASSERT_EQ(*it, expected);
            }
        }
    }
};

TEST_F(ThreadBufferTest, insert_parallel_test) {}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}