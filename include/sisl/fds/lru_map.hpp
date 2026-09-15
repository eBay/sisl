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

#include <cstddef>
#include <list>
#include <unordered_map>
#include <utility>

namespace sisl {

// Tiny bounded cache used by simple in-memory LRU scenarios.
//
// The implementation keeps the most recently touched item at the front of `order_` and
// uses `index_` to map a key to the corresponding list iterator. `std::size_t` is needed
// for the cache capacity, while `std::pair` and `std::move` are required to represent each
// cached element and move replacement values into the list without copying.
template < typename Key, typename Value >
class LruMap {
public:
    using value_type = std::pair< Key, Value >;
    using list_type = std::list< value_type >;
    using iterator = typename list_type::iterator;
    using const_iterator = typename list_type::const_iterator;

    explicit LruMap(std::size_t capacity) : capacity_{capacity} {}

    // Inserts or updates a key. Updating an existing key refreshes the value and moves it to
    // the front to preserve the recency ordering. When the cache exceeds its capacity, the
    // least recently used item is evicted from both `order_` and `index_`.
    void set(Key const& key, Value value) {
        if (auto it = index_.find(key); it != index_.end()) {
            it->second->second = std::move(value);
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        order_.emplace_front(key, std::move(value));
        index_[order_.front().first] = order_.begin();
        while (order_.size() > capacity_) {
            index_.erase(order_.back().first);
            order_.pop_back();
        }
    }

    // Missing keys return a default-constructed Value (shared_ptr -> nullptr).
    Value get(Key const& key) const {
        auto it = index_.find(key);
        if (it == index_.end()) { return Value{}; }
        return it->second->second;
    }

    const_iterator begin() const { return order_.begin(); }
    const_iterator end() const { return order_.end(); }

private:
    std::size_t capacity_;
    list_type order_;
    std::unordered_map< Key, iterator > index_;
};

} // namespace sisl
