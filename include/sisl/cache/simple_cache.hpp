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
#pragma once

#include <set>
#include <sisl/cache/evictor.hpp>
#include <sisl/cache/simple_hashmap.hpp>

using namespace std::placeholders;

namespace sisl {

template < typename K, typename V >
class SimpleCache {
private:
    std::shared_ptr< Evictor > m_evictor;
    key_extractor_cb_t< K, V > m_key_extract_cb;
    SimpleHashMap< K, V > m_map;
    uint32_t m_record_family_id;
    uint32_t m_per_value_size;

    static thread_local std::set< K > t_failed_keys;

public:
    SimpleCache(const std::shared_ptr< Evictor >& evictor, uint32_t num_buckets, uint32_t per_val_size,
                key_extractor_cb_t< K, V >&& extract_cb, Evictor::eviction_cb_t evict_cb = nullptr) :
            m_evictor{evictor},
            m_key_extract_cb{std::move(extract_cb)},
            m_map{num_buckets, m_key_extract_cb, std::bind(&SimpleCache< K, V >::on_hash_operation, this, _1, _2, _3)},
            m_per_value_size{per_val_size} {
                // Register the record family callbacks with the evictor:
                // - `can_evict_cb`: Provided by the user of the `SimpleCache`. This callback determines whether a record can be evicted.
                // - `post_eviction_cb`: Owned by the `SimpleCache`. This callback is used to remove the evicted record from the hashmap.
                //   Note: We cannot directly call `erase` on the hashmap as it might result in deadlocks. Instead, we use `try_erase`,
                //   which attempts to acquire the bucket lock using `try_lock`. If the lock cannot be acquired, the method returns `false`.
                //   In such cases, we notify the evictor to skip evicting this record and try the next one.
        m_record_family_id = m_evictor->register_record_family(Evictor::RecordFamily{.can_evict_cb = evict_cb
            , .post_eviction_cb = [this](const CacheRecord& record) {
                V const value = reinterpret_cast<SingleEntryHashNode< V >*>(const_cast< CacheRecord* >(&record))->m_value;
                K key = m_key_extract_cb(value);
                return m_map.try_erase(key);
            }});
    }

    ~SimpleCache() { m_evictor->unregister_record_family(m_record_family_id); }

    bool insert(const V& value) {
        K k = m_key_extract_cb(value);
        bool ret = m_map.insert(k, value);
        if (t_failed_keys.size()) {
            // There are some failures to add for some keys
            for (auto& key : t_failed_keys) {
                V dummy_v;
                m_map.erase(key, dummy_v);
                ret = false;
            }
            t_failed_keys.clear();
        }
        return ret;
    }

    bool upsert(const V& value) {
        K k = m_key_extract_cb(value);
        return m_map.upsert(k, value);
    }

    bool remove(const K& key, V& out_val) { return m_map.erase(key, out_val); }

    bool get(const K& key, V& out_val) { return m_map.get(key, out_val); }

private:
    void on_hash_operation(const CacheRecord& r, const K& key, const hash_op_t op) {
        CacheRecord& record = const_cast< CacheRecord& >(r);
        const auto hash_code = SimpleHashMap< K, V >::compute_hash(key);

        switch (op) {
        case hash_op_t::CREATE:
            record.set_record_family(m_record_family_id);
            record.set_size(m_per_value_size);
            if (!m_evictor->add_record(hash_code, record)) {
                // We were not able to evict any, so mark this record and we will erase them upon all callbacks are done
                t_failed_keys.insert(key);
            }
            break;

        case hash_op_t::DELETE:
            if (t_failed_keys.size()) {
                // Check if this is a delete of failed keys, if so lets not add it to record
                if (t_failed_keys.find(key) != t_failed_keys.end()) { return; }
            }
            m_evictor->remove_record(hash_code, record);
            break;

        case hash_op_t::ACCESS:
            m_evictor->record_accessed(hash_code, record);
            break;

        case hash_op_t::RESIZE: {
            DEBUG_ASSERT(false, "Don't expect RESIZE operation for simple cache entries");
            break;
        }
        default:
            DEBUG_ASSERT(false, "Invalid hash_op");
            break;
        }
    }
};

template < typename K, typename V >
thread_local std::set< K > SimpleCache< K, V >::t_failed_keys;

} // namespace sisl
