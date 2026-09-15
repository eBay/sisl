/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
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
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include <gtest/gtest.h>

#include <sisl/fds/bounded_mpmc_queue.hpp>

using namespace sisl;

SISL_OPTIONS_ENABLE(logging, test_bounded_mpmc_queue)
SISL_OPTION_GROUP(test_bounded_mpmc_queue,
                  (num_threads, "", "num_threads", "number of producer/consumer threads",
                   ::cxxopts::value< uint32_t >()->default_value("4"), "number"),
                  (num_entries, "", "num_entries", "number of entries per producer thread",
                   ::cxxopts::value< uint32_t >()->default_value("5000"), "number"))

TEST(BoundedMPMCQueueTest, WriteAndReadSingleValue) {
    BoundedMPMCQueue< int > q{4};
    EXPECT_EQ(q.sizeGuess(), 0u);

    EXPECT_TRUE(q.write(42));
    EXPECT_EQ(q.sizeGuess(), 1u);

    int out{0};
    EXPECT_TRUE(q.read(out));
    EXPECT_EQ(out, 42);
    EXPECT_EQ(q.sizeGuess(), 0u);
}

TEST(BoundedMPMCQueueTest, ReadFailsWhenEmpty) {
    BoundedMPMCQueue< int > q{4};
    int out{0};
    EXPECT_FALSE(q.read(out));
}

TEST(BoundedMPMCQueueTest, WriteFailsWhenFull) {
    constexpr size_t capacity{4};
    BoundedMPMCQueue< int > q{capacity};

    for (size_t i{0}; i < capacity; ++i) {
        EXPECT_TRUE(q.write(static_cast< int >(i)));
    }
    EXPECT_EQ(q.sizeGuess(), capacity);
    EXPECT_FALSE(q.write(999));
    EXPECT_EQ(q.sizeGuess(), capacity);

    int out{0};
    EXPECT_TRUE(q.read(out));
    EXPECT_EQ(out, 0);
    EXPECT_TRUE(q.write(999));
    EXPECT_EQ(q.sizeGuess(), capacity);
}

TEST(BoundedMPMCQueueTest, PreservesFifoOrder) {
    BoundedMPMCQueue< int > q{8};
    for (int i{0}; i < 8; ++i) {
        EXPECT_TRUE(q.write(i));
    }

    for (int i{0}; i < 8; ++i) {
        int out{-1};
        EXPECT_TRUE(q.read(out));
        EXPECT_EQ(out, i);
    }
}

TEST(BoundedMPMCQueueTest, ConcurrentMultiProducerMultiConsumer) {
    auto const num_threads = SISL_OPTIONS["num_threads"].as< uint32_t >();
    auto const num_entries = SISL_OPTIONS["num_entries"].as< uint32_t >();
    auto const total_entries = num_threads * num_entries;

    BoundedMPMCQueue< uint64_t > q{16};
    std::vector< std::atomic< bool > > received(total_entries);
    for (auto& r : received) {
        r.store(false);
    }
    std::atomic< uint32_t > consumed_count{0};

    std::vector< std::thread > producers;
    for (uint32_t t{0}; t < num_threads; ++t) {
        producers.emplace_back([&q, t, num_entries]() {
            for (uint32_t i{0}; i < num_entries; ++i) {
                const uint64_t value{static_cast< uint64_t >(t) * num_entries + i};
                while (!q.write(value)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::vector< std::thread > consumers;
    for (uint32_t t{0}; t < num_threads; ++t) {
        consumers.emplace_back([&q, &received, &consumed_count, total_entries]() {
            uint64_t value{0};
            while (consumed_count.load(std::memory_order_relaxed) < total_entries) {
                if (q.read(value)) {
                    ASSERT_LT(value, total_entries);
                    ASSERT_FALSE(received[value].exchange(true));
                    consumed_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& thr : producers) {
        thr.join();
    }
    for (auto& thr : consumers) {
        thr.join();
    }

    EXPECT_EQ(consumed_count.load(), total_entries);
    EXPECT_EQ(q.sizeGuess(), 0u);
    for (const auto& r : received) {
        EXPECT_TRUE(r.load());
    }
}

int main(int argc, char* argv[]) {
    int parsed_argc{argc};
    ::testing::InitGoogleTest(&parsed_argc, argv);
    SISL_OPTIONS_LOAD(parsed_argc, argv, logging, test_bounded_mpmc_queue);

    sisl::logging::SetLogger("test_bounded_mpmc_queue");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    return RUN_ALL_TESTS();
}
