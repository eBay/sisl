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

#include <array>
#include <mutex>
#include <functional>
#include <sisl/logging/logging.h>
#include <sisl/cache/hash_entry_base.hpp>
#include <spdlog/fmt/fmt.h>

namespace sisl {
typedef ValueEntryBase CacheRecord;

class Evictor {
public:
    typedef std::function< bool(const CacheRecord&) > eviction_cb_t;
    using can_evict_cb_t = eviction_cb_t;

    // struct to hold the eviction callbacks for each record family
    // can_evict_cb: called before eviction to check if the record can be evicted.
    // post_eviction_cb: called after eviction to do any cleanup. If this returns false, the record is reinserted.
    // and we try to evict the next record.
    struct RecordFamily {
        Evictor::eviction_cb_t can_evict_cb{nullptr};
        Evictor::eviction_cb_t post_eviction_cb{nullptr};
    };

    Evictor(const int64_t max_size, const uint32_t num_partitions) :
            m_max_size{max_size}, m_num_partitions{num_partitions} {}
    Evictor(const Evictor&) = delete;
    Evictor(Evictor&&) noexcept = delete;
    Evictor& operator=(const Evictor&) = delete;
    Evictor& operator=(Evictor&&) noexcept = delete;
    virtual ~Evictor() = default;

    uint32_t register_record_family(RecordFamily record_family) {
        uint32_t id{0};
        std::unique_lock lk(m_reg_mtx);
        while (id < m_eviction_cbs.size()) {
            if (m_eviction_cbs[id].first == false) {
                m_eviction_cbs[id] = std::make_pair(true, record_family);
                return id;
            }
            ++id;
        }
        RELEASE_ASSERT(false, "More than {} record types registered", CacheRecord::max_record_families());
        return 0;
    }

    void unregister_record_family(const uint32_t record_type_id) {
        std::unique_lock lk(m_reg_mtx);
        m_eviction_cbs[record_type_id] = std::make_pair(false, RecordFamily{});
    }

    void add_metrics(CacheMetrics* metrics) {
        m_metrics = metrics;
    }
    CacheMetrics* metrics_ptr() {
        return m_metrics;
    }

    virtual bool add_record(uint64_t hash_code, CacheRecord& record) = 0;
    virtual void remove_record(uint64_t hash_code, CacheRecord& record) = 0;
    virtual void record_accessed(uint64_t hash_code, CacheRecord& record) = 0;
    virtual void record_resized(uint64_t hash_code, const CacheRecord& record, uint32_t old_size) = 0;

    int64_t max_size() const { return m_max_size; }
    uint32_t num_partitions() const { return m_num_partitions; }
    const eviction_cb_t& can_evict_cb(const uint32_t record_id) const { return m_eviction_cbs[record_id].second.can_evict_cb; }
    const eviction_cb_t& post_eviction_cb(const uint32_t record_id) const { return m_eviction_cbs[record_id].second.post_eviction_cb; }

private:
    int64_t m_max_size;
    uint32_t m_num_partitions;

    std::mutex m_reg_mtx;
    std::array< std::pair< bool /*registered*/, RecordFamily >, CacheRecord::max_record_families() > m_eviction_cbs;
    // metrics raw ptr, we do not own it
    CacheMetrics* m_metrics{nullptr};
};
} // namespace sisl
