#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <type_traits>

#include "histogram_buckets.hpp"
#include "metrics_group_impl.hpp"

namespace sisl {
static_assert(std::is_trivially_copyable< HistogramValue >::value, "Expecting HistogramValue to be trivally copyable");

class AtomicCounterValue {
public:
    AtomicCounterValue() = default;
    AtomicCounterValue(const AtomicCounterValue&) = delete;
    AtomicCounterValue(AtomicCounterValue&&) noexcept = delete;
    AtomicCounterValue& operator=(const AtomicCounterValue&) = delete;
    AtomicCounterValue& operator=(AtomicCounterValue&&) noexcept = delete;

    void increment(const int64_t value = 1) { m_value.fetch_add(value, std::memory_order_relaxed); }
    void decrement(const int64_t value = 1) { m_value.fetch_sub(value, std::memory_order_relaxed); }
    [[nodiscard]] int64_t get() const { return m_value.load(std::memory_order_relaxed); }

    [[nodiscard]] CounterValue to_counter_value() const {
        CounterValue v{};
        v.m_value = get();
        return v;
    }

private:
    std::atomic< int64_t > m_value{0};
};

class AtomicHistogramValue {
public:
    AtomicHistogramValue() = default;
    AtomicHistogramValue(const AtomicHistogramValue&) = delete;
    AtomicHistogramValue(AtomicHistogramValue&&) noexcept = delete;
    AtomicHistogramValue& operator=(const AtomicHistogramValue&) = delete;
    AtomicHistogramValue& operator=(AtomicHistogramValue&&) noexcept = delete;

    void observe(const int64_t value, const hist_bucket_boundaries_t& boundaries, const uint64_t count = 1) {
        const auto lower{std::lower_bound(std::cbegin(boundaries), std::cend(boundaries),
                                          value)};
        if (lower != std::cend(boundaries)) {
            const auto bkt_idx{std::distance(std::cbegin(boundaries), lower)};
            m_freqs[bkt_idx].fetch_add(count, std::memory_order_relaxed);
            m_sum.fetch_add((value * count), std::memory_order_relaxed);
        }
    }

    [[nodiscard]] auto& get_freqs() const { return m_freqs; }
    [[nodiscard]] int64_t get_sum() const {
        return m_sum.load(std::memory_order_relaxed); }

    [[nodiscard]] HistogramValue to_histogram_value() const {
        HistogramValue h{};
        std::copy(std::cbegin(m_freqs), std::cend(m_freqs), std::begin(h.m_freqs));
        h.m_sum = get_sum();
        return h;
    }

private:
    std::array< std::atomic< int64_t >, HistogramBuckets::max_hist_bkts > m_freqs{0};
    std::atomic< int64_t > m_sum{0};
};

class AtomicMetricsGroup : public MetricsGroupImpl {
public:
    AtomicMetricsGroup(const char* const grp_name, const char* const inst_name) : MetricsGroupImpl(grp_name, inst_name) {}
    AtomicMetricsGroup(const std::string& grp_name, const std::string& inst_name) :
            MetricsGroupImpl(grp_name, inst_name) {}
    virtual ~AtomicMetricsGroup() = default;
    AtomicMetricsGroup(const AtomicMetricsGroup&) = delete;
    AtomicMetricsGroup(AtomicMetricsGroup&&) noexcept = delete;
    AtomicMetricsGroup& operator=(const AtomicMetricsGroup&) = delete;
    AtomicMetricsGroup& operator=(AtomicMetricsGroup&&) noexcept = delete;

    void counter_increment(const uint64_t index, const int64_t val = 1) override;
    void counter_decrement(const uint64_t index, const int64_t val = 1) override;
    void histogram_observe(const uint64_t index, const int64_t val) override;
    void histogram_observe(const uint64_t index, const int64_t val, const uint64_t count) override;

    [[nodiscard]] group_impl_type_t impl_type() const { return group_impl_type_t::atomic; }

private:
    void on_register();
    void gather_result(bool need_latest, const counter_gather_cb_t& counter_cb, const gauge_gather_cb_t& gauge_cb,
                       const histogram_gather_cb_t& histogram_cb) override;

private:
    std::unique_ptr< AtomicCounterValue[] > m_counter_values;
    std::unique_ptr< AtomicHistogramValue[] > m_histogram_values;
};
} // namespace sisl