/*
 * Created by Hari Kadayam on Sept 24 2019
 */

#pragma once

#include <folly/SharedMutex.h>
#include <boost/dynamic_bitset.hpp>
#include "metrics/metrics.hpp"

namespace sisl {
class StreamTrackerMetrics : public MetricsGroupWrapper {
public:
    explicit StreamTrackerMetrics(const char* inst_name) : MetricsGroupWrapper("StreamTracker", inst_name) {
        REGISTER_COUNTER(stream_tracker_unsweeped_completions, "How many completions are unsweeped yet", "", {"", ""},
                         publish_as_gauge);

        REGISTER_GAUGE(stream_tracker_mem_size, "Total Memsize for stream tracker");
        REGISTER_GAUGE(stream_tracker_completed_upto, "Idx upto which stream tracker cursor is completed");

        register_me_to_farm();
    }
};

template < typename T, bool AutoSweep = false >
class StreamTracker {
    using data_processing_t = std::function< bool(T&) >;

public:
    static constexpr size_t alloc_blk_size = 10000;

    // Initialize the stream vector with start index
    StreamTracker(const char* name = "StreamTracker", int64_t start_idx = -1) : m_metrics(name) {
        m_slot_ref_idx = start_idx + 1;

        // Set the size of the slots to
        m_comp_slot_bits.resize(alloc_blk_size);
        m_comp_slot_bits.reset();

        m_active_slot_bits.resize(alloc_blk_size);
        m_active_slot_bits.reset();

        // Allocate the data for that size.
        m_slot_data = (T*)std::calloc(alloc_blk_size, sizeof(T));
        m_alloced_slots = alloc_blk_size;
        GAUGE_UPDATE(m_metrics, stream_tracker_mem_size, (m_alloced_slots * sizeof(T)));
    }

    ~StreamTracker() {
        GAUGE_UPDATE(m_metrics, stream_tracker_mem_size, 0);
        free(m_slot_data);
    }

    template < class... Args >
    bool set(int64_t idx, Args&&... args) {
        return do_update(idx, nullptr, true /* replace */, std::forward< Args >(args)...);
    }

    template < class... Args >
    bool update(int64_t idx, const data_processing_t& processor, Args&&... args) {
        return do_update(idx, processor, false /* replace */, std::forward< Args >(args)...);
    }

    T& at(int64_t idx) const {
        folly::SharedMutexWritePriority::ReadHolder holder(m_lock);
        if (idx < m_slot_ref_idx) { throw std::out_of_range(); }

        size_t nbit = idx - m_slot_ref_idx;
        if (!m_active_slot_bits[nbit]) { throw std::out_of_range(); }
        return m_slot_data[nbit];
    }

    size_t sweep() {
        folly::SharedMutexWritePriority::WriteHolder holder(m_lock);

        if (AutoSweep && (m_cmpltd_count_since_last_sweep.load(std::memory_order_acquire) == 0)) { return 0; }
        // assert(m_farthest_active_slot > 0);

        // Find the first bit with 0 in it
        auto first_incomplete_bit = (~m_comp_slot_bits).find_first();
        if (first_incomplete_bit == boost::dynamic_bitset<>::npos) {
            // Seems like all the bits are completed, set it as last
            first_incomplete_bit = m_alloced_slots;
        } else if (first_incomplete_bit == 0) {
            // Nothing is completed, nothing to sweep
            return m_slot_ref_idx - 1;
        }

        // Move all the bits upto the first unacked bit
        m_comp_slot_bits >>= first_incomplete_bit;
        m_active_slot_bits >>= first_incomplete_bit;

        // If we have some entities at the right of unacked bit, then we need to move them upto that.
        if (m_farthest_active_slot > first_incomplete_bit) {
            auto remain_count = m_farthest_active_slot - first_incomplete_bit;
            std::memmove((void*)&m_slot_data[0], (void*)&m_slot_data[first_incomplete_bit], (sizeof(T) * remain_count));
            m_farthest_active_slot = remain_count;
        } else {
            // All the cursors are acked, so no remaining cursors
            m_farthest_active_slot = 0;
        }

        // auto prev_ref_idx = m_slot_ref_idx;
        m_slot_ref_idx += first_incomplete_bit;
        COUNTER_DECREMENT(m_metrics, stream_tracker_unsweeped_completions, first_incomplete_bit);

        // TODO: Do a callback on how much has been moved forward to
        // m_on_sweep_cb(m_slot_ref_idx - prev_ref_idx);

        return m_slot_ref_idx - 1;
    }

    int64_t completed_upto(bool up_to_date) {
        if (up_to_date) {
            return sweep();
        } else {
            folly::SharedMutexWritePriority::ReadHolder holder(m_lock);
            return m_slot_ref_idx - 1;
        }
    }

private:
    template < class... Args >
    bool do_update(int64_t idx, const data_processing_t& processor, bool replace, Args&&... args) {
        bool need_sweep = false;

        size_t nbit;
        do {
            m_lock.lock_shared();

            // In case we got an update for older idx which was already swept, return right away
            if (idx < m_slot_ref_idx) {
                m_lock.unlock_shared();
                return false;
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
        if (replace || !m_active_slot_bits[nbit]) {
            // First time being updated, so use placement new to use the slot to build data
            data = new (&m_slot_data[nbit]) T(std::forward< Args >(args)...);
            m_active_slot_bits[nbit] = true;
        } else {
            data = &m_slot_data[nbit];
        }

        // Adjust the farthest active slot
        if (m_farthest_active_slot < nbit) { m_farthest_active_slot = nbit; }

        // Check with processor to update any fields and return if they are completed
        if ((processor == nullptr) || processor(*data)) {
            // All actions on this idx is completed, sweep if needbe
            m_comp_slot_bits[nbit] = true;
            if (AutoSweep) {
                if (m_cmpltd_count_since_last_sweep.fetch_add(1, std::memory_order_acq_rel) >= m_sweep_on_count) {
                    need_sweep = true;
                }
            }
            COUNTER_INCREMENT(m_metrics, stream_tracker_unsweeped_completions, 1);
        }
        m_lock.unlock_shared();

        if (need_sweep) { sweep(); }
        return true;
    }

    void do_resize(size_t atleast_count) {
        folly::SharedMutexWritePriority::WriteHolder holder(m_lock);

        // Check if we already resized enough
        if (atleast_count < m_alloced_slots) { return; }

        auto new_count = std::max((m_alloced_slots * 2), atleast_count);
        m_slot_data = (T*)std::realloc((void*)m_slot_data, sizeof(T) * new_count);
        if (m_slot_data == nullptr) { throw std::bad_alloc(); }

        // auto extra_count = new_count - m_alloced_slots;
        // std::memset(&m_slot_data[m_alloced_slots], 0, (sizeof(T) * extra_count));
        m_active_slot_bits.resize(new_count);
        m_comp_slot_bits.resize(new_count);
        m_alloced_slots = new_count;

        GAUGE_UPDATE(m_metrics, stream_tracker_mem_size, (m_alloced_slots * sizeof(T)));
    }

private:
    // Mutex to protect the completion of last commit info
    folly::SharedMutexWritePriority m_lock;

    // A bitset that covers the completion and sweep
    boost::dynamic_bitset<> m_comp_slot_bits;

    // A bitset that tracks which are active or completed.
    boost::dynamic_bitset<> m_active_slot_bits;

    // The array of cursors which are being processed right now. We purposefully didn't want to use std::vector
    // because, completion bits sweeps everything at one shot and we don't want iterating one after other under
    // lock.
    T* m_slot_data = nullptr;

    // Total number of slots allocated
    size_t m_alloced_slots = 0;

    // Total number of actual items present in the slots array
    size_t m_farthest_active_slot = 0;

    // Total number of entries completely acked (for all txns) since last sweep
    std::atomic< size_t > m_cmpltd_count_since_last_sweep = 0;

    // Reference idx of the stream. This is the cursor idx which it is tracking
    int64_t m_slot_ref_idx = 0;

    // How frequent (on count) sweep needs to happen
    uint32_t m_sweep_on_count = 1000;

    StreamTrackerMetrics m_metrics;
};
} // namespace sisl