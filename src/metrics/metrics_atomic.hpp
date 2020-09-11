#pragma once

#include "histogram_buckets.hpp"
#include <atomic>
#include <array>
#include <cstdint>
#include <algorithm>
#include "metrics_group_impl.hpp"

namespace sisl {
static_assert(std::is_trivially_copyable< HistogramValue >::value, "Expecting HistogramValue to be trivally copyable");

class AtomicCounterValue {
public:
    AtomicCounterValue() = default;
    void increment(int64_t value = 1) { m_value.fetch_add(value, std::memory_order_relaxed); }
    void decrement(int64_t value = 1) { m_value.fetch_sub(value, std::memory_order_relaxed); }
    int64_t get() const { return m_value.load(std::memory_order_relaxed); }

    CounterValue to_counter_value() const {
        CounterValue v;
        v.m_value = get();
        return v;
    }

private:
    std::atomic< int64_t > m_value = 0;
};

class AtomicHistogramValue {
public:
    void observe(int64_t value, const hist_bucket_boundaries_t& boundaries, uint64_t count = 1) {
        auto lower = std::lower_bound(boundaries.begin(), boundaries.end(), value);
        auto bkt_idx = lower - boundaries.begin();
        m_freqs[bkt_idx].fetch_add(count, std::memory_order_relaxed);
        m_sum.fetch_add((value * count), std::memory_order_relaxed);
    }

    auto& get_freqs() const { return m_freqs; }
    int64_t get_sum() const { return m_sum.load(std::memory_order_relaxed); }

    auto to_histogram_value() {
        HistogramValue h;
        memcpy((void*)&h.m_freqs, (void*)&m_freqs, HistogramBuckets::max_hist_bkts * sizeof(int64_t));
        h.m_sum = get_sum();
        return h;
    }

private:
    std::array< std::atomic< int64_t >, HistogramBuckets::max_hist_bkts > m_freqs;
    std::atomic< int64_t > m_sum = 0;
};

class AtomicMetricsGroup : public MetricsGroupImpl {
public:
    AtomicMetricsGroup(const char* grp_name, const char* inst_name) : MetricsGroupImpl(grp_name, inst_name) {}
    AtomicMetricsGroup(const std::string& grp_name, const std::string& inst_name) :
            MetricsGroupImpl(grp_name, inst_name) {}

    virtual ~AtomicMetricsGroup() = default;
    void counter_increment(uint64_t index, int64_t val = 1) override;
    void counter_decrement(uint64_t index, int64_t val = 1) override;
    void histogram_observe(uint64_t index, int64_t val) override;
    void histogram_observe(uint64_t index, int64_t val, uint64_t count) override;

    group_impl_type_t impl_type() const { return group_impl_type_t::atomic; }

private:
    void on_register();
    void gather_result(bool need_latest, const counter_gather_cb_t& counter_cb, const gauge_gather_cb_t& gauge_cb,
                       const histogram_gather_cb_t& histogram_cb) override;

private:
    std::unique_ptr< AtomicCounterValue[] > m_counter_values;
    std::unique_ptr< AtomicHistogramValue[] > m_histogram_values;
};
} // namespace sisl