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

#include <mutex>
#include <vector>
#include <functional>
#include <memory>
#include <boost/intrusive/list.hpp>
#include <sisl/fds/utils.hpp>
#include "evictor.hpp"

using namespace boost::intrusive;

namespace sisl {

class LRUEvictor : public Evictor {
public:
    typedef std::function< bool(const ValueEntryBase&) > can_evict_cb_t;

    LRUEvictor(const int64_t max_size, const uint32_t num_partitions);
    LRUEvictor(const LRUEvictor&) = delete;
    LRUEvictor(LRUEvictor&&) noexcept = delete;
    LRUEvictor& operator=(const LRUEvictor&) = delete;
    LRUEvictor& operator=(LRUEvictor&&) noexcept = delete;
    virtual ~LRUEvictor() = default;

    bool add_record(uint64_t hash_code, CacheRecord& record) override;
    void remove_record(uint64_t hash_code, CacheRecord& record) override;

    /* Upvote the entry. This depends on the current rank will move up and thus reduce the chances of getting evicted.
     * In case of LRU allocation, it moves to the tail end of the list. The entry is expected to be present in the
     * eviction list */
    void record_accessed(uint64_t hash_code, CacheRecord& record) override;

    void record_resized(uint64_t hash_code, const CacheRecord& record, uint32_t old_size) override;

private:
    typedef list<
        ValueEntryBase,
        member_hook< ValueEntryBase, list_member_hook< link_mode< auto_unlink > >, &ValueEntryBase::m_member_hook >,
        constant_time_size< false > >
        EvictRecordList;

    class LRUPartition {
    private:
        EvictRecordList m_list;
        LRUEvictor* m_evictor;
        std::mutex m_list_guard;
        uint32_t m_partition_num;
        int64_t m_filled_size{0};
        int64_t m_max_size;

    public:
        LRUPartition() = default;
        LRUPartition(const LRUPartition&) = delete;
        LRUPartition& operator=(const LRUPartition&) = delete;
        LRUPartition(LRUPartition&&) = default;
        LRUPartition& operator=(LRUPartition&&) = default;

        void init(LRUEvictor* evictor, const uint32_t partition_num, const uint64_t max_size) {
            m_evictor = evictor;
            m_partition_num = partition_num;
            m_max_size = int64_cast(max_size);
        }
        bool add_record(CacheRecord& record);
        void remove_record(CacheRecord& record);
        void record_accessed(CacheRecord& record);
        void record_resized(const CacheRecord& record, uint32_t old_size);

    private:
        bool do_evict(const uint32_t record_fid, const uint32_t needed_size);
        bool will_fill(const uint32_t new_size) const { return ((m_filled_size + new_size) > m_max_size); }
        bool is_full() const { return will_fill(0); }
    };

private:
    LRUPartition& get_partition(uint64_t hash_code) { return m_partitions[hash_code % num_partitions()]; }
    const LRUPartition& get_partition_const(uint64_t hash_code) const {
        return m_partitions[hash_code % num_partitions()];
    }
    std::unique_ptr< LRUPartition[] > m_partitions;
};
} // namespace sisl
