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

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "sisl/fds/mcmp_priority_queue.hpp"

TEST(MPMCPriorityQueueTest, BasicPushPop) {
    sisl::MPMCPriorityQueue< int > queue;

    // Push elements
    queue.push(5);
    queue.push(2);
    queue.push(8);
    queue.push(1);

    EXPECT_EQ(queue.size(), 4);
    EXPECT_FALSE(queue.empty());

    // Pop in priority order (max heap by default)
    auto r1 = queue.pop();
    EXPECT_TRUE(r1.is_ok());
    EXPECT_EQ(r1.value.value(), 8);

    auto r2 = queue.pop();
    EXPECT_TRUE(r2.is_ok());
    EXPECT_EQ(r2.value.value(), 5);

    auto r3 = queue.pop();
    EXPECT_TRUE(r3.is_ok());
    EXPECT_EQ(r3.value.value(), 2);

    auto r4 = queue.pop();
    EXPECT_TRUE(r4.is_ok());
    EXPECT_EQ(r4.value.value(), 1);

    EXPECT_EQ(queue.size(), 0);
    EXPECT_TRUE(queue.empty());
}

TEST(MPMCPriorityQueueTest, CustomComparator) {
    // Min-heap using std::greater
    sisl::MPMCPriorityQueue< int, std::greater< int > > queue;

    queue.push(5);
    queue.push(2);
    queue.push(8);
    queue.push(1);

    // Pop in ascending order
    EXPECT_EQ(queue.pop().value.value(), 1);
    EXPECT_EQ(queue.pop().value.value(), 2);
    EXPECT_EQ(queue.pop().value.value(), 5);
    EXPECT_EQ(queue.pop().value.value(), 8);
}

TEST(MPMCPriorityQueueTest, MoveSemantics) {
    struct MoveOnly {
        int value;

        explicit MoveOnly(int v) : value(v) {}
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&&) = default;
        MoveOnly& operator=(MoveOnly&&) = default;

        bool operator<(const MoveOnly& other) const { return value < other.value; }
    };

    sisl::MPMCPriorityQueue< MoveOnly > queue;

    queue.push(MoveOnly(5));
    queue.push(MoveOnly(2));
    queue.push(MoveOnly(8));

    EXPECT_EQ(queue.pop().value.value().value, 8);
    EXPECT_EQ(queue.pop().value.value().value, 5);
    EXPECT_EQ(queue.pop().value.value().value, 2);
}

// ============================================================================
// Close Operation Tests
// ============================================================================

TEST(MPMCPriorityQueueTest, Close) {
    sisl::MPMCPriorityQueue< int > queue;

    queue.push(1);
    queue.push(2);
    queue.push(3);

    EXPECT_FALSE(queue.is_closed());
    queue.close();
    EXPECT_TRUE(queue.is_closed());

    // Can still pop existing elements
    EXPECT_EQ(queue.pop().value.value(), 3);
    EXPECT_EQ(queue.pop().value.value(), 2);
    EXPECT_EQ(queue.pop().value.value(), 1);

    // Now should return Closed status
    auto result = queue.pop();
    EXPECT_TRUE(result.is_closed());
    EXPECT_FALSE(result.value.has_value());
}

TEST(MPMCPriorityQueueTest, PushAfterClose) {
    sisl::MPMCPriorityQueue< int > queue;

    queue.push(1);
    queue.close();

    // Pushes after close are ignored
    queue.push(2);
    queue.push(3);

    EXPECT_EQ(queue.size(), 1);

    auto r1 = queue.pop();
    EXPECT_TRUE(r1.is_ok());
    EXPECT_EQ(r1.value.value(), 1);

    auto r2 = queue.pop();
    EXPECT_TRUE(r2.is_closed());
}

TEST(MPMCPriorityQueueTest, CloseIdempotent) {
    sisl::MPMCPriorityQueue< int > queue;

    queue.push(1);
    queue.close();
    queue.close(); // Should be safe
    queue.close();

    EXPECT_TRUE(queue.is_closed());
    EXPECT_EQ(queue.size(), 1);
}

// ============================================================================
// Blocking Behavior Tests
// ============================================================================

TEST(MPMCPriorityQueueTest, BlockingPop) {
    sisl::MPMCPriorityQueue< int > queue;
    std::atomic< bool > pop_started{false};
    std::atomic< bool > pop_completed{false};

    // Consumer thread that will block
    std::thread consumer([&]() {
        pop_started = true;
        auto result = queue.pop();
        pop_completed = true;

        EXPECT_TRUE(result.is_ok());
        EXPECT_EQ(result.value.value(), 42);
    });

    // Wait for consumer to start
    while (!pop_started) {
        std::this_thread::yield();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(pop_completed);

    // Unblock consumer by pushing
    queue.push(42);

    consumer.join();
    EXPECT_TRUE(pop_completed);
}

TEST(MPMCPriorityQueueTest, CloseUnblocksWaiters) {
    sisl::MPMCPriorityQueue< int > queue;
    std::atomic< int > closed_count{0};

    // Start multiple waiting consumers
    std::vector< std::thread > consumers;
    for (int i = 0; i < 5; ++i) {
        consumers.emplace_back([&]() {
            auto result = queue.pop();
            if (result.is_closed()) { closed_count.fetch_add(1, std::memory_order_relaxed); }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Close should wake all waiters
    queue.close();

    for (auto& t : consumers) {
        t.join();
    }

    EXPECT_EQ(closed_count.load(), 5);
}

// ============================================================================
// Multi-threaded Producer Tests
// ============================================================================

TEST(MPMCPriorityQueueTest, MultipleProducers) {
    sisl::MPMCPriorityQueue< int > queue;
    constexpr int num_producers = 4;
    constexpr int items_per_producer = 250;

    std::vector< std::thread > producers;
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&, i]() {
            for (int j = 0; j < items_per_producer; ++j) {
                queue.push(i * items_per_producer + j);
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }

    EXPECT_EQ(queue.size(), num_producers * items_per_producer);

    // Verify all elements come out in descending order
    std::vector< int > popped;
    for (int i = 0; i < num_producers * items_per_producer; ++i) {
        auto result = queue.pop();
        ASSERT_TRUE(result.is_ok());
        popped.push_back(result.value.value());
    }

    EXPECT_TRUE(std::is_sorted(popped.rbegin(), popped.rend()));
}

// ============================================================================
// Multi-threaded Consumer Tests
// ============================================================================

TEST(MPMCPriorityQueueTest, MultipleConsumers) {
    sisl::MPMCPriorityQueue< int > queue;
    constexpr int num_items = 1000;

    // Fill queue
    for (int i = 0; i < num_items; ++i) {
        queue.push(i);
    }

    constexpr int num_consumers = 4;
    std::vector< std::thread > consumers;
    std::atomic< int > total_consumed{0};

    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&]() {
            int count = 0;
            while (true) {
                auto result = queue.pop();
                if (result.is_closed()) { break; }
                ++count;
            }
            total_consumed.fetch_add(count, std::memory_order_relaxed);
        });
    }

    // Give consumers time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Close to signal completion
    queue.close();

    for (auto& t : consumers) {
        t.join();
    }

    EXPECT_EQ(total_consumed.load(), num_items);
}

// ============================================================================
// Concurrent Producers and Consumers
// ============================================================================

TEST(MPMCPriorityQueueTest, ConcurrentProducersConsumers) {
    sisl::MPMCPriorityQueue< int > queue;
    constexpr int num_producers = 3;
    constexpr int num_consumers = 3;
    constexpr int items_per_producer = 200;

    std::atomic< int > total_consumed{0};
    std::vector< std::thread > threads;

    // Start consumers
    for (int i = 0; i < num_consumers; ++i) {
        threads.emplace_back([&]() {
            int count = 0;
            while (true) {
                auto result = queue.pop();
                if (result.is_closed()) { break; }
                ++count;
            }
            total_consumed.fetch_add(count, std::memory_order_relaxed);
        });
    }

    // Start producers
    for (int i = 0; i < num_producers; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < items_per_producer; ++j) {
                queue.push(i * items_per_producer + j);
                std::this_thread::sleep_for(std::chrono::microseconds(10)); // Simulate work
            }
        });
    }

    // Wait for producers
    for (int i = num_consumers; i < num_consumers + num_producers; ++i) {
        threads[i].join();
    }

    // Close and wait for consumers
    queue.close();
    for (int i = 0; i < num_consumers; ++i) {
        threads[i].join();
    }

    EXPECT_EQ(total_consumed.load(), num_producers * items_per_producer);
}

// ============================================================================
// Stress Test
// ============================================================================

TEST(MPMCPriorityQueueTest, StressTest) {
    sisl::MPMCPriorityQueue< int > queue;
    constexpr int num_threads = 8;
    constexpr int operations_per_thread = 1000;

    std::atomic< int > push_count{0};
    std::atomic< int > pop_count{0};
    std::vector< std::thread > threads;

    // Half producers, half consumers
    for (int i = 0; i < num_threads / 2; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < operations_per_thread; ++j) {
                queue.push(j);
                push_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (int i = 0; i < num_threads / 2; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < operations_per_thread; ++j) {
                auto result = queue.pop();
                if (result.is_ok()) { pop_count.fetch_add(1, std::memory_order_relaxed); }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(push_count.load(), (num_threads / 2) * operations_per_thread);

    // Pop remaining elements
    while (!queue.empty()) {
        auto result = queue.pop();
        if (result.is_ok()) { pop_count.fetch_add(1, std::memory_order_relaxed); }
    }

    EXPECT_EQ(pop_count.load(), push_count.load());
}

// ============================================================================
// Destructor Test
// ============================================================================

TEST(MPMCPriorityQueueTest, DestructorClosesQueue) {
    std::atomic< bool > consumer_unblocked{false};

    std::thread consumer([&]() {
        auto queue = std::make_unique< sisl::MPMCPriorityQueue< int > >();
        queue->push(1);

        std::thread waiter([&, q = queue.get()]() {
            auto first_result = q->pop(); // Pop the 1
            (void)first_result;           // Explicitly ignore the result
            auto result = q->pop();       // This will block until destructor closes queue
            if (result.is_closed()) { consumer_unblocked = true; }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Destructor will be called here
        queue.reset();

        waiter.join();
    });

    consumer.join();
    EXPECT_TRUE(consumer_unblocked);
}

TEST(MPMCPriorityQueueTest, PopsHighestPriorityElementFirst) {
    sisl::MPMCPriorityQueue< int > queue;

    EXPECT_TRUE(queue.push(5));
    EXPECT_TRUE(queue.push(2));
    EXPECT_TRUE(queue.push(9));
    EXPECT_TRUE(queue.push(7));

    auto first = queue.pop();
    ASSERT_TRUE(first.is_ok());
    ASSERT_TRUE(first.value.has_value());
    EXPECT_EQ(first.value.value(), 9);

    auto second = queue.pop();
    ASSERT_TRUE(second.is_ok());
    ASSERT_TRUE(second.value.has_value());
    EXPECT_EQ(second.value.value(), 7);

    auto third = queue.pop();
    ASSERT_TRUE(third.is_ok());
    ASSERT_TRUE(third.value.has_value());
    EXPECT_EQ(third.value.value(), 5);

    EXPECT_EQ(queue.size(), 1u);
}

TEST(MPMCPriorityQueueTest, TryPopReportsEmptyUntilClosed) {
    sisl::MPMCPriorityQueue< int > queue;

    auto empty = queue.try_pop();
    EXPECT_TRUE(empty.is_empty());
    EXPECT_FALSE(empty.value.has_value());

    EXPECT_TRUE(queue.push(42));

    auto item = queue.try_pop();
    ASSERT_TRUE(item.is_ok());
    ASSERT_TRUE(item.value.has_value());
    EXPECT_EQ(item.value.value(), 42);

    auto empty_again = queue.try_pop();
    EXPECT_TRUE(empty_again.is_empty());
    EXPECT_FALSE(empty_again.value.has_value());

    queue.close();
    auto closed = queue.try_pop();
    EXPECT_TRUE(closed.is_closed());
    EXPECT_FALSE(closed.value.has_value());
}

TEST(MPMCPriorityQueueTest, CloseRejectsNewPushesAndDrainsExistingItems) {
    sisl::MPMCPriorityQueue< int > queue;

    EXPECT_TRUE(queue.push(10));
    EXPECT_TRUE(queue.push(20));

    queue.close();
    EXPECT_FALSE(queue.push(30));
    EXPECT_TRUE(queue.is_closed());

    auto first = queue.pop();
    ASSERT_TRUE(first.is_ok());
    ASSERT_TRUE(first.value.has_value());
    EXPECT_EQ(first.value.value(), 20);

    auto second = queue.pop();
    ASSERT_TRUE(second.is_ok());
    ASSERT_TRUE(second.value.has_value());
    EXPECT_EQ(second.value.value(), 10);

    auto done = queue.pop();
    EXPECT_TRUE(done.is_closed());
    EXPECT_FALSE(done.value.has_value());
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
