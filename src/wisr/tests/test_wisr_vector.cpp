/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "sisl/utility/thread_buffer.hpp"
#include "sisl/wisr/wisr_ds.hpp"

THREAD_BUFFER_INIT
RCU_REGISTER_INIT

using namespace sisl;

static constexpr size_t INITIAL_THREADS{8};
static constexpr size_t ADDLN_THREADS{4};
static constexpr size_t ENTRIES_PER_THREAD{10000};
static constexpr size_t TOTAL_THREADS{INITIAL_THREADS + ADDLN_THREADS};
[[maybe_unused]] static constexpr size_t TOTAL_ENTRIES{ENTRIES_PER_THREAD * TOTAL_THREADS};

struct WaitFreeWriteVectorTest : public testing::Test {
public:
    WaitFreeWriteVectorTest() : m_vec(1000), m_write_threads_completed(0) {
        for (size_t i{0}; i < INITIAL_THREADS; ++i) {
            m_threads.emplace_back(write_thread, i, this);
        }
        m_scrapper_thread = std::thread(scrapper_thread, this);
        std::this_thread::yield();
    }

    virtual ~WaitFreeWriteVectorTest() override {
        if (m_scrapper_thread.joinable()) m_scrapper_thread.join();

        for (auto& t : m_threads) {
            if (t.joinable()) t.join();
        }
    }

protected:
    std::mutex m_print_mutex;
    std::vector< std::thread > m_threads;
    std::thread m_scrapper_thread;
    sisl::wisr_vector< uint64_t > m_vec;
    std::atomic< uint32_t > m_write_threads_completed;

    void SetUp() override {}
    void TearDown() override {}

    static void write_thread(const uint32_t thread_index, WaitFreeWriteVectorTest* const test) {
        static thread_local std::random_device rd{};
        static thread_local std::default_random_engine engine{rd()};
        std::uniform_int_distribution< uint64_t > sleep_time{0, (thread_index + 1) * 100 - 1};

        const uint64_t lhs_mask{static_cast< uint64_t >(thread_index) << 32};
        for (size_t i{0}; i < ENTRIES_PER_THREAD; ++i) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_time(engine)));
            const uint64_t val{lhs_mask | i}; // Make sure entry is unique across the thread by putting thread as lhs
            test->m_vec.push_back(val);
        }
        {
            std::scoped_lock< std::mutex > lock{test->m_print_mutex};
            std::cout << "Thread " << thread_index << " done writing" << std::endl;
        }
        ++(test->m_write_threads_completed);
    }

    static void scrape(WaitFreeWriteVectorTest* const test, std::vector< uint64_t >& result_vec) {
        auto vec_copy{test->m_vec.get_copy_and_reset()};
        result_vec.insert(std::end(result_vec), std::cbegin(*vec_copy), std::cend(*vec_copy));
        {
            std::scoped_lock< std::mutex > lock{test->m_print_mutex};
            std::cout << "Scrapped " << vec_copy->size()
                      << " in this iteration. total_entries_recvd so far = " << result_vec.size() << std::endl;
        }
    }

    static void scrapper_thread(WaitFreeWriteVectorTest* const test) {
        std::vector< uint64_t > final_vec;

        do {
            std::this_thread::sleep_for(std::chrono::microseconds(50000));
            scrape(test, final_vec);

            // Start any remaining new threads
            for (size_t i{test->m_threads.size()}; i < TOTAL_THREADS; ++i) {
                test->m_threads.emplace_back(write_thread, i, test);
            }

            if (test->m_write_threads_completed.load() == TOTAL_THREADS) {
                // All threads finished writing, scrape once and check for missing entries
                scrape(test, final_vec);
                find_missing(test, final_vec);
                break;
            }
        } while (true);
    }

    static void find_missing(WaitFreeWriteVectorTest* const test, std::vector< uint64_t >& result) {
        std::sort(std::begin(result), std::end(result));
        auto it{std::cbegin(result)};
        for (size_t i{0}; i < test->m_threads.size(); ++i) {
            const uint64_t lhs_mask{static_cast< uint64_t >(i) << 32};
            for (size_t i2{0}; i2 < ENTRIES_PER_THREAD; ++i2) {
                const uint64_t expected{lhs_mask | i2};
                if (expected != *it) {
                    EXPECT_EQ(*it, expected);
                    {
                        std::scoped_lock< std::mutex > lock{test->m_print_mutex};
                        std::cout << "Missing " << expected << std::endl;
                    }
                } else {
                    ++it;
                }
            }
        }
    }
};

TEST_F(WaitFreeWriteVectorTest, insert_parallel_test) {}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
