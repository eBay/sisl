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
 * Unless required by applicable law or agreed to in writing, software
 *distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 *License for the specific language governing permissions and limitations under
 *the License.
 *
 *********************************************************************************/
#include <sisl/cache/lru_evictor.hpp>

namespace sisl {

LRUEvictor::LRUEvictor(const int64_t max_size, const uint32_t num_partitions)
    : Evictor(max_size, num_partitions) {
  m_partitions = std::make_unique<LRUPartition[]>(num_partitions);
  for (uint32_t i{0}; i < num_partitions; ++i) {
    m_partitions[i].init(this, i, uint64_cast(max_size / num_partitions));
  }
}

bool LRUEvictor::add_record(uint64_t hash_code, CacheRecord &record) {
  return get_partition(hash_code).add_record(record);
}

void LRUEvictor::remove_record(uint64_t hash_code, CacheRecord &record) {
  get_partition(hash_code).remove_record(record);
}

void LRUEvictor::record_accessed(uint64_t hash_code, CacheRecord &record) {
  get_partition(hash_code).record_accessed(record);
}

void LRUEvictor::record_resized(uint64_t hash_code, const CacheRecord &record,
                                uint32_t old_size) {
  get_partition(hash_code).record_resized(record, old_size);
}

bool LRUEvictor::LRUPartition::add_record(CacheRecord &record) {
  std::unique_lock guard{m_list_guard};
  if (will_fill(record.size())) {
    if (!do_evict(record.record_family_id(), record.size())) {
      return false;
    }
  }
  m_list.push_back(record);
  m_filled_size += record.size();
  if (m_evictor->metrics_ptr()) {
    COUNTER_INCREMENT(*(m_evictor->metrics_ptr()), cache_object_count, 1);
    COUNTER_INCREMENT(*(m_evictor->metrics_ptr()), cache_size, record.size());
  }
  return true;
}

void LRUEvictor::LRUPartition::remove_record(CacheRecord &record) {
  std::unique_lock guard{m_list_guard};

  // accessing the iterator to the record crashes if the record is not present in the list.
  if (!record.m_member_hook.is_linked()) {
    LOGERROR("Not expected! Record not found in partition {}", m_partition_num);
    DEBUG_ASSERT(false, "Not expected! Record not found");
    return;
  }
  auto it = m_list.iterator_to(record);
  m_filled_size -= record.size();
  m_list.erase(it);
  if (m_evictor->metrics_ptr()) {
    COUNTER_DECREMENT(*(m_evictor->metrics_ptr()), cache_object_count, 1);
    COUNTER_DECREMENT(*(m_evictor->metrics_ptr()), cache_size, record.size());
  }
}

void LRUEvictor::LRUPartition::record_accessed(CacheRecord &record) {
  std::unique_lock guard{m_list_guard};

  // accessing the iterator to the record crashes if the record is not present in the list.
  if (!record.m_member_hook.is_linked()) {
    LOGERROR("Not Expected! Record not found in partition {}", m_partition_num);
    DEBUG_ASSERT(false, "Not expected! Record not found");
    return;
  }
  m_list.erase(m_list.iterator_to(record));
  m_list.push_back(record);
}

void LRUEvictor::LRUPartition::record_resized(const CacheRecord &record,
                                              const uint32_t old_size) {
  std::unique_lock guard{m_list_guard};
  m_filled_size -= (record.size() - old_size);
}

bool LRUEvictor::LRUPartition::do_evict(const uint32_t record_fid,
                                        const uint32_t needed_size) {
  size_t eviction_punt_count{0};
  size_t evictions_count{0};
  size_t evicted_size{0};

  auto it = std::begin(m_list);
  while (will_fill(needed_size) && (it != std::end(m_list))) {
    CacheRecord &rec = *it;
    bool eviction_failed{true};
    /* return the next element */
    if (!rec.is_pinned() && (!m_evictor->can_evict_cb(record_fid) || m_evictor->can_evict_cb(record_fid)(rec))) {
      auto const rec_size = rec.size();
      it = m_list.erase(it);
      if (m_evictor->post_eviction_cb(record_fid) && !m_evictor->post_eviction_cb(record_fid)(rec)) {
          // If the post eviction callback fails, we need to reinsert the record
          // back into the list.
          it = m_list.insert(it, rec);
      } else {
        eviction_failed = false;
        m_filled_size -= rec_size;
        evictions_count++;
        evicted_size += rec_size;
      }
    }
    
    if (eviction_failed) {
        ++eviction_punt_count;
        it = std::next(it);
    }
  }

  if (eviction_punt_count > 0) {
    LOGDEBUG("LRU ejection had to skip {} entries", eviction_punt_count);
    if (m_evictor->metrics_ptr()) {
      COUNTER_INCREMENT(*(m_evictor->metrics_ptr()), cache_num_evictions_punt, eviction_punt_count);
    }
  }
  if (is_full()) {
    // No available candidate to evict
    LOGERROR("No cache space available: Eviction partition={} as "
             "total_entries={} rejected eviction request to add "
             "size={}, already filled={}, num evictions punt={}",
             m_partition_num, m_list.size(), needed_size, m_filled_size, eviction_punt_count);
    return false;
  }

  if (m_evictor->metrics_ptr()) {
    COUNTER_INCREMENT(*(m_evictor->metrics_ptr()), cache_num_evictions, evictions_count);
    COUNTER_DECREMENT(*(m_evictor->metrics_ptr()), cache_object_count, evictions_count);
    COUNTER_DECREMENT(*(m_evictor->metrics_ptr()), cache_size, evicted_size);
  }
  return true;
}
} // namespace sisl
