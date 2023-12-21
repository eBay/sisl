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
#include <algorithm>

#include <folly/SharedMutex.h>
#include <sisl/metrics/metrics_group_impl.hpp>
#include <sisl/metrics/metrics.hpp>

#include "bitset.hpp"

namespace sisl {
class StreamTrackerMetrics : public MetricsGroupWrapper {
public:
    explicit StreamTrackerMetrics(const char* inst_name) : MetricsGroupWrapper("StreamTracker", inst_name) {
        REGISTER_COUNTER(stream_tracker_unsweeped_completions, "How many completions are unsweeped yet", "", {"", ""},
                         _publish_as::publish_as_gauge);

        REGISTER_GAUGE(stream_tracker_mem_size, "Total Memsize for stream tracker");
        REGISTER_GAUGE(stream_tracker_completed_upto, "Idx upto which stream tracker cursor is completed");

        register_me_to_farm();
    }
    ~StreamTrackerMetrics() { deregister_me_from_farm(); }
};

template < typename T, bool AutoTruncate = false >
class StreamTracker {
    // using data_processing_t = std::function< bool(T&) >;

public:
    static constexpr size_t alloc_blk_size = 10000;
    static constexpr size_t compaction_threshold = alloc_blk_size / 2;
    static constexpr auto null_processor = []([[maybe_unused]] auto... x) -> bool { return true; };

    static_assert(std::is_trivially_copyable< T >::value, "Cannot use StreamTracker for non-trivally copyable classes");

    // Initialize the stream vector with start index
    StreamTracker(const char* name = "StreamTracker", int64_t start_idx = -1) :
            m_comp_slot_bits(alloc_blk_size), m_active_slot_bits(alloc_blk_size), m_metrics(name) {
        m_slot_ref_idx = start_idx + 1;

        // Allocate the data for that size.
        m_slot_data = (T*)std::calloc(alloc_blk_size, sizeof(T));
        m_alloced_slots = alloc_blk_size;
        GAUGE_UPDATE(m_metrics, stream_tracker_mem_size, (m_alloced_slots * sizeof(T)));
    }

    ~StreamTracker() {
        GAUGE_UPDATE(m_metrics, stream_tracker_mem_size, 0);
        free(m_slot_data);
    }

    void reinit(int64_t start_idx) { m_slot_ref_idx = start_idx; }

    template < class... Args >
    int64_t create_and_complete(int64_t idx, Args&&... args) {
        return do_update(idx, null_processor, true /* replace */, std::forward< Args >(args)...);
    }

    template < class... Args >
    int64_t create(int64_t idx, Args&&... args) {
        return do_update(
            idx, []([[maybe_unused]] T& data) { return false; }, true /* replace */, std::forward< Args >(args)...);
    }

    template < class... Args >
    int64_t update(int64_t idx, const auto& processor, Args&&... args) {
        return do_update(idx, processor, false /* replace */, std::forward< Args >(args)...);
    }

    void complete(int64_t start_idx, int64_t end_idx) {
        folly::SharedMutexWritePriority::ReadHolder holder(m_lock);
        auto start_bit = start_idx - m_slot_ref_idx;
        m_comp_slot_bits.set_bits(start_bit, end_idx - start_idx + 1);
    }

    void rollback(int64_t new_end_idx) {
        folly::SharedMutexWritePriority::ReadHolder holder(m_lock);
        if ((new_end_idx < m_slot_ref_idx) ||
            (new_end_idx >= (m_slot_ref_idx + int64_cast(m_active_slot_bits.size())))) {
            throw std::out_of_range("Slot idx is not in range");
        }

        auto new_end_bit = new_end_idx - m_slot_ref_idx;
        m_active_slot_bits.reset_bits(new_end_bit + 1, m_active_slot_bits.size() - new_end_bit - 1);
        m_comp_slot_bits.reset_bits(new_end_bit + 1, m_comp_slot_bits.size() - new_end_bit - 1);
    }

    T& at(int64_t idx) const {
        folly::SharedMutexWritePriority::ReadHolder holder(m_lock);
        if (idx < m_slot_ref_idx) { throw std::out_of_range("Slot idx is not in range"); }

        size_t nbit = idx - m_slot_ref_idx;
        if (!m_active_slot_bits.get_bitval(nbit)) { throw std::out_of_range("Slot idx is not in range"); }
        return *get_slot_data(nbit);
    }

    /* Returns an anonymous structure which has 3 fields
     * is_out_of_range: Is the entry out_of_range of whats been tracked
     * is_hole: Is the entry is in_range but no data has been created or completed.
     * is_active: Is the entry valid and current active
     * is_completed: Is the entry valid and completed
     */
    auto status(int64_t idx) const {
        struct {
            bool is_out_of_range = false;
            bool is_hole = false;
            bool is_active = false;
            bool is_completed = false;
        } ret;

        folly::SharedMutexWritePriority::ReadHolder holder(m_lock);
        if (idx < m_slot_ref_idx) {
            ret.is_out_of_range = true;
        } else {
            size_t nbit = idx - m_slot_ref_idx;
            if (m_comp_slot_bits.get_bitval(nbit)) {
                ret.is_completed = true;
            } else if (m_active_slot_bits.get_bitval(nbit)) {
                ret.is_active = true;
            } else {
                ret.is_hole = true;
            }
        }
        return ret;
    }

    size_t truncate(int64_t idx) {
        folly::SharedMutexWritePriority::WriteHolder holder(m_lock);

        auto upto_bit = idx - m_slot_ref_idx + 1;
        if (upto_bit <= 0) { return m_slot_ref_idx - 1; }
        return do_truncate(upto_bit);
    }

    size_t truncate() {
        if (AutoTruncate && (m_cmpltd_count_since_last_truncate.load(std::memory_order_acquire) == 0)) { return 0; }

        folly::SharedMutexWritePriority::WriteHolder holder(m_lock);

        // Find the first bit with 0 in it
        auto first_incomplete_bit = m_comp_slot_bits.get_next_reset_bit(0);
        if (first_incomplete_bit == AtomicBitset::npos) {
            // Seems like all the bits are completed, set it as last
            first_incomplete_bit = m_alloced_slots;
        } else if (first_incomplete_bit == 0) {
            // Nothing is completed, nothing to truncate
            return m_slot_ref_idx - 1;
        }
        return do_truncate(first_incomplete_bit);
    }

    size_t do_truncate(int64_t upto_bit) {
        // Move all the bits upto the first incomplete bit
        m_comp_slot_bits.shrink_head(upto_bit);
        m_active_slot_bits.shrink_head(upto_bit);

        // Shrink the data as well upto first_incomplete_bit. Instead of memmoving every truncate which could also
        // be frequent, we simply mark to skip that much data and then when we are really needed we compact them.
        m_data_skip_count += upto_bit;
        m_alloced_slots -= upto_bit;
        if (m_data_skip_count > compaction_threshold) {
            std::memmove((void*)&m_slot_data[0], (void*)&m_slot_data[m_data_skip_count], (sizeof(T) * m_alloced_slots));
            m_data_skip_count = 0;
        }

        // auto prev_ref_idx = m_slot_ref_idx;
        m_slot_ref_idx += upto_bit;
        COUNTER_DECREMENT(m_metrics, stream_tracker_unsweeped_completions, upto_bit);

        // TODO: Do a callback on how much has been moved forward to
        // m_on_sweep_cb(m_slot_ref_idx - prev_ref_idx);

        return m_slot_ref_idx - 1;
    }

    void foreach_contiguous_completed(int64_t start_idx, const auto& cb) { _foreach_contiguous(start_idx, true, cb); }
    void foreach_contiguous_active(int64_t start_idx, const auto& cb) { _foreach_contiguous(start_idx, false, cb); }
    void foreach_all_completed(int64_t start_idx, const auto& cb) { _foreach_all(start_idx, true, cb); }
    void foreach_all_active(int64_t start_idx, const auto& cb) { _foreach_all(start_idx, false, cb); }

    int64_t completed_upto(int64_t search_hint_idx = 0) const {
        folly::SharedMutexWritePriority::ReadHolder holder(m_lock);
        return _upto(true /* completed */, search_hint_idx);
    }

    int64_t active_upto(int64_t search_hint_idx = 0) const {
        folly::SharedMutexWritePriority::ReadHolder holder(m_lock);
        return _upto(false /* completed */, search_hint_idx);
    }

    nlohmann::json get_status(const int verbosity) const {
        nlohmann::json js;
        js["start"] = m_slot_ref_idx;
        js["completed_upto"] = completed_upto();
        js["active_upto"] = active_upto();

        if (verbosity == 2) {
            js["alloced_count"] = m_alloced_slots;
            if (AutoTruncate) {
                js["completed_since_last_truncate"] =
                    m_cmpltd_count_since_last_truncate.load(std::memory_order_relaxed);
            }
            js["truncate_frequency"] = m_truncate_on_count;
            js["garbage_count"] = m_data_skip_count;
        }
        return js;
    }

private:
    template < class... Args >
    int64_t do_update(int64_t idx, const auto& processor, bool replace, Args&&... args) {
        bool need_truncate = false;
        int64_t ret = 0;
        size_t nbit;

        do {
            m_lock.lock_shared();

            // In case we got an update for older idx which was already swept, return right away
            if (idx < m_slot_ref_idx) {
                ret = m_slot_ref_idx - 1;
                m_lock.unlock_shared();
                return ret;
            }

            nbit = idx - m_slot_ref_idx;
            if (nbit >= m_alloced_slots) {
                m_lock.unlock_shared();
                do_resize(nbit + 1);
            } else {
                break;
            }
        } while (true);

        T* data;
        if (replace || !m_active_slot_bits.get_bitval(nbit)) {
            // First time being updated, so use placement new to use the slot to build data
            data = new ((void*)get_slot_data(nbit)) T(std::forward< Args >(args)...);
            m_active_slot_bits.set_bit(nbit);
        } else {
            data = get_slot_data(nbit);
        }

        // Check with processor to update any fields and return if they are completed
        if (processor(*data)) {
            // All actions on this idx is completed, truncate if needbe
            m_comp_slot_bits.set_bit(nbit);
            if (AutoTruncate) {
                if (m_cmpltd_count_since_last_truncate.fetch_add(1, std::memory_order_acq_rel) >= m_truncate_on_count) {
                    need_truncate = true;
                }
            }
            COUNTER_INCREMENT(m_metrics, stream_tracker_unsweeped_completions, 1);
        }
        ret = m_slot_ref_idx - 1;
        m_lock.unlock_shared();

        if (need_truncate) { ret = truncate(); }
        return ret;
    }

    void do_resize(size_t atleast_count) {
        folly::SharedMutexWritePriority::WriteHolder holder(m_lock);

        // Check if we already resized enough
        if (atleast_count < m_alloced_slots) { return; }

        auto new_count = std::max((m_alloced_slots * 2), atleast_count);
        auto new_slot_data = (T*)std::calloc(new_count, sizeof(T));
        if (new_slot_data == nullptr) { throw std::bad_alloc(); }

        std::memmove((void*)&new_slot_data[0], (void*)&m_slot_data[m_data_skip_count], (sizeof(T) * m_alloced_slots));
        free(m_slot_data);
        m_slot_data = new_slot_data;
        m_alloced_slots = new_count;
        m_data_skip_count = 0;

        m_active_slot_bits.resize(new_count);
        m_comp_slot_bits.resize(new_count);

        GAUGE_UPDATE(m_metrics, stream_tracker_mem_size, (m_alloced_slots * sizeof(T)));
    }

    int64_t _upto(bool completed, int64_t search_hint_idx) const {
        auto search_start_bit = std::max(0ll, (search_hint_idx - m_slot_ref_idx));
        auto first_incomplete_bit = completed ? m_comp_slot_bits.get_next_reset_bit(search_start_bit)
                                              : m_active_slot_bits.get_next_reset_bit(search_start_bit);
        if (first_incomplete_bit == AtomicBitset::npos) {
            // Seems like all the bits are completed, set it as last
            return m_slot_ref_idx + m_alloced_slots - 1;
        } else {
            return m_slot_ref_idx + first_incomplete_bit - 1;
        }
    }

    void _foreach_contiguous(int64_t start_idx, bool completed_only, const auto& cb) {
        folly::SharedMutexWritePriority::ReadHolder holder(m_lock);
        auto upto = _upto(completed_only, start_idx);
        for (auto idx = start_idx; idx <= upto; ++idx) {
            auto proceed = cb(idx, upto, *(get_slot_data(idx - m_slot_ref_idx)));
            if (!proceed) break;
        }
    }

    void _foreach_all(int64_t start_idx, bool completed_only, const auto& cb) {
        folly::SharedMutexWritePriority::ReadHolder holder(m_lock);
        auto search_bit = std::max(0l, (start_idx - m_slot_ref_idx));
        do {
            search_bit = completed_only ? m_comp_slot_bits.get_next_set_bit(search_bit)
                                        : m_active_slot_bits.get_next_set_bit(search_bit);
            if (search_bit == AtomicBitset::npos) { break; }
            if (!cb(search_bit + m_slot_ref_idx, *(get_slot_data(search_bit)))) { break; }
            ++search_bit;
        } while (true);
    }

    T* get_slot_data(int64_t nbit) const { return &(m_slot_data[nbit + m_data_skip_count]); }

private:
    // Mutex to protect the completion of last commit info
    mutable folly::SharedMutexWritePriority m_lock;

    // A bitset that covers the completion and truncation
    sisl::AtomicBitset m_comp_slot_bits;

    // A bitset that tracks which are active or completed.
    sisl::AtomicBitset m_active_slot_bits;

    // The array of cursors which are being processed right now. We purposefully didn't want to use std::vector
    // because, completion bits truncates everything at one shot and we don't want iterating one after other under
    // lock.
    T* m_slot_data{nullptr};

    // Amount of data to skip before m_slot_data[0] index starts. This is done to avoid memmove entire data set
    // everytime it is truncated. Instead it leaves the buffer as is and then shrinks fewer a between
    size_t m_data_skip_count{0};

    // Total number of slots allocated
    size_t m_alloced_slots{0};

    // Total number of entries completely acked (for all txns) since last truncate
    std::atomic< size_t > m_cmpltd_count_since_last_truncate{0};

    // Reference idx of the stream. This is the cursor idx which it is tracking
    int64_t m_slot_ref_idx{0};

    // How frequent (on count) truncate needs to happen
    uint32_t m_truncate_on_count{1000};

    StreamTrackerMetrics m_metrics;
};
} // namespace sisl
