#pragma once

#include "histogram_buckets.hpp"
#include <atomic>
#include <array>
#include "metrics_group_impl.hpp"

namespace sisl {
static_assert(std::is_trivially_copyable< HistogramValue >::value, "Expecting HistogramValue to be trivally copyable");

class HistogramInfo;
class PerThreadMetrics {
private:
    CounterValue* m_counters = nullptr;
    HistogramValue* m_histograms = nullptr;
    const std::vector< HistogramInfo >& m_histogram_info;

    uint32_t m_ncntrs;
    uint32_t m_nhists;

public:
    PerThreadMetrics(const std::vector< HistogramInfo >& hinfo, uint32_t ncntrs, uint32_t nhists);
    ~PerThreadMetrics();

    static void merge(PerThreadMetrics* a, PerThreadMetrics* b);
    CounterValue& get_counter(uint64_t index);
    HistogramValue& get_histogram(uint64_t index);

    auto get_num_metrics() const { return std::make_tuple(m_ncntrs, m_nhists); }
};

using PerThreadMetricsBuffer =
    ExitSafeThreadBuffer< PerThreadMetrics, const std::vector< HistogramInfo >&, uint32_t, uint32_t >;

class ThreadBufferMetricsGroup : public MetricsGroupImpl {
public:
    ThreadBufferMetricsGroup(const char* grp_name, const char* inst_name) : MetricsGroupImpl(grp_name, inst_name) {}
    ThreadBufferMetricsGroup(const std::string& grp_name, const std::string& inst_name) :
            MetricsGroupImpl(grp_name, inst_name) {}

    void counter_increment(uint64_t index, int64_t val = 1) override;
    void counter_decrement(uint64_t index, int64_t val = 1) override;
    void histogram_observe(uint64_t index, int64_t val) override;
    void histogram_observe(uint64_t index, int64_t val, uint64_t count) override;

    static void flush_core_cache();

    group_impl_type_t impl_type() const { return group_impl_type_t::thread_buf_signal; }

private:
    void on_register();
    void gather_result(bool need_latest, std::function< void(CounterInfo&, const CounterValue&) > counter_cb,
                       std::function< void(GaugeInfo&) > gauge_cb,
                       std::function< void(HistogramInfo&, const HistogramValue&) > histogram_cb) override;

private:
    std::unique_ptr< PerThreadMetricsBuffer > m_metrics_buf;
    std::unique_ptr< PerThreadMetrics > m_gather_metrics;
};
} // namespace sisl