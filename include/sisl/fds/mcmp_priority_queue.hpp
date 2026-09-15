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
#pragma once

#include <condition_variable>
#include <concepts>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

namespace sisl {

/**
 * @brief Multi-Producer Multi-Consumer Priority Queue (C++20)
 *
 * Thread-safe priority queue that supports:
 * - Concurrent push operations from multiple producers
 * - Concurrent pop operations from multiple consumers
 * - Blocking pop when queue is empty
 * - Graceful shutdown via close() method
 *
 * @tparam T Element type (must be comparable)
 * @tparam Compare Comparison function (default: std::less for max-heap)
 */
template < typename T, typename Compare = std::less< T > >
    requires std::movable< T > && std::predicate< Compare, T, T >
class MPMCPriorityQueue {
public:
    using value_type = T;
    using size_type = std::size_t;
    using comparator_type = Compare;

    /**
     * @brief Status codes returned by pop operations
     */
    enum class Status : uint8_t {
        Ok,    ///< Successfully popped an element
        Empty, ///< Queue is empty but still open (try_pop only)
        Closed ///< Queue is closed, no more elements available
    };

    /**
     * @brief Result of a pop operation
     */
    struct PopResult {
        Status status;
        std::optional< T > value; ///< Has value only if status == Ok

        // Convenience methods
        [[nodiscard]] constexpr bool is_ok() const noexcept { return status == Status::Ok; }
        [[nodiscard]] constexpr bool is_empty() const noexcept { return status == Status::Empty; }
        [[nodiscard]] constexpr bool is_closed() const noexcept { return status == Status::Closed; }
    };

    /**
     * @brief Construct an empty priority queue
     */
    constexpr MPMCPriorityQueue() noexcept(std::is_nothrow_default_constructible_v< Compare >) = default;

    /**
     * @brief Destructor - automatically closes the queue
     *
     * @note Blocks until every thread currently blocked in pop() has woken up and returned from
     * std::condition_variable::wait(). Destroying mutex_/cv_ while a thread is still inside wait() is undefined
     * behavior even after notify_all() has been called, since the woken thread may still be re-acquiring the
     * lock internally.
     */
    ~MPMCPriorityQueue() {
        close();
        std::unique_lock lock(mutex_);
        no_waiters_cv_.wait(lock, [this] { return waiters_ == 0; });
    }

    // Disable copy and move to prevent issues with condition variables
    MPMCPriorityQueue(const MPMCPriorityQueue&) = delete;
    MPMCPriorityQueue& operator=(const MPMCPriorityQueue&) = delete;
    MPMCPriorityQueue(MPMCPriorityQueue&&) = delete;
    MPMCPriorityQueue& operator=(MPMCPriorityQueue&&) = delete;

    /**
     * @brief Thread-safe push operation (copy)
     *
     * @param value Element to insert
     * @return true if pushed successfully, false if queue is closed
     */
    bool push(const T& value)
        requires std::copy_constructible< T >
    {
        {
            std::scoped_lock lock(mutex_);
            if (closed_) [[unlikely]] {
                return false; // Queue is closed, cannot push
            }
            pq_.push(value);
        }
        cv_.notify_one(); // Wake one waiting consumer
        return true;
    }

    /**
     * @brief Thread-safe push operation (move)
     *
     * @param value Element to insert (will be moved)
     * @return true if pushed successfully, false if queue is closed
     */
    bool push(T&& value) {
        {
            std::scoped_lock lock(mutex_);
            if (closed_) [[unlikely]] { return false; }
            pq_.push(std::move(value));
        }
        cv_.notify_one();
        return true;
    }

    /**
     * @brief Thread-safe pop operation
     *
     * Blocks if queue is empty and not closed.
     * Returns immediately if queue is closed.
     *
     * @return PopResult containing status and optional value
     * @note Thread-safe for multiple concurrent consumers
     */
    [[nodiscard]] PopResult pop() {
        std::unique_lock lock(mutex_);

        // Wait until queue has elements or is closed
        ++waiters_;
        cv_.wait(lock, [this] { return closed_ || !pq_.empty(); });
        if (--waiters_ == 0) { no_waiters_cv_.notify_all(); }

        // Try to pop an element
        if (!pq_.empty()) {
            T top = std::move(const_cast< T& >(pq_.top()));
            pq_.pop();
            return PopResult{.status = Status::Ok, .value = std::move(top)};
        }

        // Queue is empty and closed
        return PopResult{.status = Status::Closed, .value = std::nullopt};
    }

    /// Non-blocking pop. Safe to call from an iomgr reactor (never waits).
    [[nodiscard]] PopResult try_pop() {
        std::scoped_lock lock(mutex_);
        if (!pq_.empty()) {
            T top = std::move(const_cast< T& >(pq_.top()));
            pq_.pop();
            return PopResult{.status = Status::Ok, .value = std::move(top)};
        }
        if (closed_) { return PopResult{.status = Status::Closed, .value = std::nullopt}; }
        return PopResult{.status = Status::Empty, .value = std::nullopt};
    }

    /**
     * @brief Close the queue
     *
     * After calling close():
     * - All blocked pop() calls will wake up
     * - Existing elements can still be popped
     * - New push() calls will be ignored
     * - pop() returns Status::Closed when queue becomes empty
     *
     * @note Thread-safe and idempotent
     */
    void close() noexcept {
        {
            std::scoped_lock lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all(); // Wake all waiting consumers
    }

    /**
     * @brief Get current number of elements
     *
     * @return Number of elements in the queue
     * @note Thread-safe
     */
    [[nodiscard]] size_type size() const {
        std::scoped_lock lock(mutex_);
        return pq_.size();
    }

    /**
     * @brief Check if queue is empty
     *
     * @return true if queue has no elements
     * @note Thread-safe
     */
    [[nodiscard]] bool empty() const {
        std::scoped_lock lock(mutex_);
        return pq_.empty();
    }

    /**
     * @brief Check if queue is closed
     *
     * @return true if close() has been called
     * @note Thread-safe
     */
    [[nodiscard]] bool is_closed() const {
        std::scoped_lock lock(mutex_);
        return closed_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable no_waiters_cv_;
    size_type waiters_{0};
    bool closed_{false};
    std::priority_queue< T, std::vector< T >, Compare > pq_;
};

} // namespace sisl
