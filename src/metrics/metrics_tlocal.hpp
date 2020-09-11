#pragma once

#include "histogram_buckets.hpp"
#include <atomic>
#include <array>
#include <vector>
#include <cstdint>
#include <string>
#include "metrics_group_impl.hpp"

namespace sisl {
static_assert(std::is_trivially_copyable< HistogramValue >::value, "Expecting HistogramValue to be trivally copyable");

class HistogramStaticInfo;
class PerThreadMetrics {
private:
    CounterValue* m_counters = nullptr;
    HistogramValue* m_histograms = nullptr;
    const std::vector< HistogramStaticInfo >& m_histogram_info;

    uint32_t m_ncntrs;
    uint32_t m_nhists;

public:
    PerThreadMetrics(const std::vector< HistogramStaticInfo >& hinfo, uint32_t ncntrs, uint32_t nhists);
    ~PerThreadMetrics();

    static void merge(PerThreadMetrics* a, PerThreadMetrics* b);
    CounterValue& get_counter(uint64_t index);
    HistogramValue& get_histogram(uint64_t index);

    auto get_num_metrics() const { return std::make_tuple(m_ncntrs, m_nhists); }
};

using PerThreadMetricsBuffer =
    ExitSafeThreadBuffer< PerThreadMetrics, const std::vector< HistogramStaticInfo >&, uint32_t, uint32_t >;

/*
 * ThreadBufferMetricsGroup is a very fast metrics accumulator and unlike RCU, it gathers the metrics for reporting
 * much faster. The logic is simply using sisl::ThreadBuffer, which is a per thread buffer with exit safety where even
 * after thread exits, its data is protected for scrapping. The metics are accumulated on each thread and when it is
 * time to scrap the metrics, it send a signal to all threads to flush caches (using atomic fencing) and then other
 * thread reads the data. Of course there is no atomicity in fetching the accurate data, but it will be timeline
 * consistent.
 *
 * Given that it is using no locks or atomics or even rcu critical section, during collecting metrics - it is probably
 * the fastest we could get. During scrapping additional latency compared to AtomicMetricsGroup is in sending signal
 * to all threads and then read thread. The difference on that is much more closer and manageable.
 */
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
    void gather_result(bool need_latest, const counter_gather_cb_t& counter_cb, const gauge_gather_cb_t& gauge_cb,
                       const histogram_gather_cb_t& histogram_cb) override;

private:
    std::unique_ptr< PerThreadMetricsBuffer > m_metrics_buf;
    std::unique_ptr< PerThreadMetrics > m_gather_metrics;
};
} // namespace sisl