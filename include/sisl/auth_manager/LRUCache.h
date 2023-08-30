#pragma once

#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace sisl {

/**
 *
 * written by @jiankun
 *
 * A high performance LRU cache implementation.
 *
 * The cache provides two atomic operations:
 *   put(key, value): put an object into the cache.
 *   get(key): returns a optional reference to the value found by key in cache
 *
 * Important notes:
 * 1. The get() method returns a const reference, any change to the reference
 * needs to be done by a Put call.
 * 2. The put/get methods are thread safe.
 */
template < typename key_t, typename value_t >
class LRUCache {
public:
    using kv_pair_t = std::pair< key_t, value_t >;
    using list_iterator_t = typename std::list< kv_pair_t >::iterator;

    explicit LRUCache(size_t capacity) : capacity_(capacity) {}

    template < typename K, typename V >
    void put(K&& key, V&& value) {
        std::unique_lock< std::shared_mutex > l{mtx_};

        auto it = items_map_.find(key);
        if (it != items_map_.end()) {
            items_list_.erase(it->second);
            items_map_.erase(it);
        }

        items_list_.emplace_front(std::make_pair(std::forward< K >(key), std::forward< V >(value)));
        items_map_[key] = items_list_.begin();

        if (items_map_.size() > capacity_) {
            auto last = items_list_.rbegin();
            items_map_.erase(last->first);
            items_list_.pop_back();
        }
    }

    [[nodiscard]] const std::optional< std::reference_wrapper< value_t const > > get(const key_t& key) {
        std::shared_lock< std::shared_mutex > l{mtx_};

        auto it = items_map_.find(key);
        if (it == items_map_.end()) { return std::nullopt; }

        items_list_.splice(items_list_.begin(), items_list_, it->second);
        return std::optional(std::cref(it->second->second));
    }

    bool exists(const key_t& key) const {
        std::shared_lock< std::shared_mutex > l{mtx_};
        return items_map_.find(key) != items_map_.end();
    }

    [[nodiscard]] size_t size() const {
        std::shared_lock< std::shared_mutex > l{mtx_};
        return items_map_.size();
    }

private:
    std::list< kv_pair_t > items_list_;
    std::unordered_map< key_t, list_iterator_t > items_map_;
    size_t capacity_;
    mutable std::shared_mutex mtx_;
};

} // namespace sisl
