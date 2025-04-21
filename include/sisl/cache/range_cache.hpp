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
#include <sisl/cache/range_hashmap.hpp>

namespace sisl {

template < typename K >
class RangeCache {
private:
    std::shared_ptr< Evictor > m_evictor;
    RangeHashMap< K > m_map;
    uint32_t m_record_family_id;
    uint32_t m_per_value_size;

    static thread_local std::set< RangeKey< K > > t_failed_keys;

public:
    RangeCache(const std::shared_ptr< Evictor >& evictor, const uint32_t num_buckets, const uint32_t per_val_size,
               Evictor::can_evict_cb_t evict_cb = nullptr) :
            m_evictor{evictor},
            m_map{RangeHashMap< K >(num_buckets, bind_this(RangeCache< K >::extract_value, 3),
                                    bind_this(RangeCache< K >::on_hash_operation, 4))},
            m_per_value_size{per_val_size} {
        m_evictor->register_record_family(Evictor::RecordFamily{.can_evict_cb = evict_cb});
    }

    ~RangeCache() { m_evictor->unregister_record_family(m_record_family_id); }

    uint32_t insert(const K& base_key, uint32_t offset, uint32_t count, sisl::io_blob&& value) {
        uint32_t failed_count{0};
        m_map.insert(RangeKey{base_key, offset, count}, std::move(value));
        if (t_failed_keys.size()) {
            // There are some failures to add for some sub keys
            for (auto& rkey : t_failed_keys) {
                failed_count += rkey.m_count;
                m_map.erase(rkey);
            }
            t_failed_keys.clear();
        }
        return failed_count;
    }

    void remove(const K& base_key, uint32_t offset, uint32_t count) { m_map.erase(RangeKey{base_key, offset, count}); }

    std::vector< std::pair< RangeKey< K >, sisl::byte_view > > get(const K& base_key, uint32_t offset, uint32_t count) {
        return m_map.get(RangeKey{base_key, offset, count});
    }

private:
    void on_hash_operation(const CacheRecord& r, const RangeKey< K >& sub_key, const hash_op_t op, int64_t new_size) {
        CacheRecord& record = const_cast< CacheRecord& >(r);
        switch (op) {
        case hash_op_t::CREATE:
            record.set_record_family(m_record_family_id);
            record.set_size(new_size);
            if (!m_evictor->add_record(sub_key.compute_hash(), record)) {
                // We were not able to evict any, so mark this record and we will erase them upon all callbacks are done
                t_failed_keys.insert(sub_key);
            }
            break;

        case hash_op_t::DELETE:
            if (t_failed_keys.size()) {
                // Check if this is a delete of failed keys, if so lets not add it to record
                if (t_failed_keys.find(sub_key) != t_failed_keys.end()) { return; }
            }
            m_evictor->remove_record(sub_key.compute_hash(), record);
            break;

        case hash_op_t::ACCESS:
            m_evictor->record_accessed(sub_key.compute_hash(), record);
            break;

        case hash_op_t::RESIZE: {
            auto old_size = record.size();
            record.set_size(new_size);
            DEBUG_ASSERT_LE(new_size, old_size, "Expect resized cache record to be smaller size");
            m_evictor->record_resized(sub_key.compute_hash(), record, old_size);
            break;
        }
        default:
            DEBUG_ASSERT(false, "Invalid hash_op");
            break;
        }
    }

    sisl::byte_view extract_value(const sisl::byte_view& inp_bytes, uint32_t nth, uint32_t count) {
        return sisl::byte_view{inp_bytes, nth * m_per_value_size, count * m_per_value_size};
    }
};

template < typename K >
thread_local std::set< RangeKey< K > > RangeCache< K >::t_failed_keys;
} // namespace sisl
