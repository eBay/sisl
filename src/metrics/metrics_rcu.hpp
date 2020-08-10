#pragma once

#include "histogram_buckets.hpp"
#include <atomic>
#include <array>
#include "metrics_tlocal.hpp"
#include "wisr/wisr_framework.hpp"

namespace sisl {
using WisrBufferMetrics =
    sisl::wisr_framework< PerThreadMetrics, const std::vector< HistogramInfo >&, uint32_t, uint32_t >;

class WisrBufferMetricsGroup : public MetricsGroupImpl {
public:
    WisrBufferMetricsGroup(const char* grp_name, const char* inst_name) : MetricsGroupImpl(grp_name, inst_name) {}
    WisrBufferMetricsGroup(const std::string& grp_name, const std::string& inst_name) :
            MetricsGroupImpl(grp_name, inst_name) {}

    void counter_increment(uint64_t index, int64_t val = 1) override;
    void counter_decrement(uint64_t index, int64_t val = 1) override;
    void histogram_observe(uint64_t index, int64_t val) override;
    void histogram_observe(uint64_t index, int64_t val, uint64_t count) override;

    group_impl_type_t impl_type() const { return group_impl_type_t::rcu; }

private:
    void on_register();
    void gather_result(bool need_latest, std::function< void(CounterInfo&, const CounterValue&) > counter_cb,
                       std::function< void(GaugeInfo&) > gauge_cb,
                       std::function< void(HistogramInfo&, const HistogramValue&) > histogram_cb) override;

private:
    std::unique_ptr< WisrBufferMetrics > m_metrics;
};
} // namespace sisl