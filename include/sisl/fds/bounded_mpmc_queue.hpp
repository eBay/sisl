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

#include <atomic>
#include <cstddef>

#include <boost/lockfree/queue.hpp>

namespace sisl {

// Folly-free replacement for folly::MPMCQueue: a capacity-bounded boost::lockfree MPMC queue plus an
// approximate size counter (folly exposed this as sizeGuess()). The queue is bounded -- write() returns false
// when full -- which blkalloc relies on (the slab cache spills to the next level on a full level, and bounds
// total cached free-blocks). We use the default (pointer-freelist) boost::lockfree::queue pre-reserved to
// `capacity` and push only via bounded_push() (never grows past the reserved nodes): this gives bounded
// behavior WITHOUT boost::lockfree::fixed_sized<true>'s hard 65535-element cap (16-bit freelist indices),
// which the blkalloc free-block / slab-cache capacities exceed. boost::lockfree::queue requires a
// trivially-copyable element type (blk_num_t, blk_cache_entry both satisfy this). The size counter is the
// central accounting a bounded+queryable queue inherently needs; boost::lockfree itself keeps no size.
template < typename T >
class BoundedMPMCQueue {
public:
    explicit BoundedMPMCQueue(const size_t capacity) : m_q{capacity} {}

    // Non-blocking enqueue; returns false if the queue is full. (folly::MPMCQueue::write)
    bool write(const T& value) {
        if (m_q.bounded_push(value)) {
            m_size.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    // Non-blocking dequeue; returns false if the queue is empty. (folly::MPMCQueue::read)
    bool read(T& out_value) {
        if (m_q.pop(out_value)) {
            m_size.fetch_sub(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    // Approximate number of elements (racy under concurrency, like folly's). (folly::MPMCQueue::sizeGuess)
    size_t sizeGuess() const { return m_size.load(std::memory_order_relaxed); }

private:
    boost::lockfree::queue< T > m_q;
    std::atomic< size_t > m_size{0};
};

} // namespace sisl
