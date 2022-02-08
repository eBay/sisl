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
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "histogram_buckets.hpp"
#include "metrics_group_impl.hpp"

namespace sisl {
static_assert(std::is_trivially_copyable< HistogramValue >::value, "Expecting HistogramValue to be trivally copyable");

class HistogramStaticInfo;
class PerThreadMetrics {
private:
    std::unique_ptr< CounterValue[] > m_counters{nullptr};
    std::unique_ptr< HistogramValue[] > m_histograms{nullptr};
    const std::vector< HistogramStaticInfo >& m_histogram_info;

    uint32_t m_ncntrs;
    uint32_t m_nhists;

public:
    PerThreadMetrics(const std::vector< HistogramStaticInfo >& hinfo, const uint32_t ncntrs, const uint32_t nhists);
    PerThreadMetrics(const PerThreadMetrics&) = delete;
    PerThreadMetrics(PerThreadMetrics&&) noexcept = delete;
    PerThreadMetrics& operator=(const PerThreadMetrics&) = delete;
    PerThreadMetrics& operator=(PerThreadMetrics&&) noexcept = delete;
    ~PerThreadMetrics();

    static void merge(PerThreadMetrics* const a, PerThreadMetrics* const b);
    CounterValue& get_counter(const uint64_t index);
    HistogramValue& get_histogram(const uint64_t index);

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
    ThreadBufferMetricsGroup(const char* const grp_name, const char* const inst_name) :
            MetricsGroupImpl{grp_name, inst_name} {}
    ThreadBufferMetricsGroup(const std::string& grp_name, const std::string& inst_name) :
            MetricsGroupImpl{grp_name, inst_name} {}

    ThreadBufferMetricsGroup(const ThreadBufferMetricsGroup&) = delete;
    ThreadBufferMetricsGroup(ThreadBufferMetricsGroup&&) noexcept = delete;
    ThreadBufferMetricsGroup& operator=(const ThreadBufferMetricsGroup&) = delete;
    ThreadBufferMetricsGroup& operator=(ThreadBufferMetricsGroup&&) noexcept = delete;
    ~ThreadBufferMetricsGroup();

    void counter_increment(const uint64_t index, const int64_t val = 1) override;
    void counter_decrement(const uint64_t index, const int64_t val = 1) override;
    void histogram_observe(const uint64_t index, const int64_t val) override;
    void histogram_observe(const uint64_t index, const int64_t val, const uint64_t count) override;

    static void flush_core_cache();

    group_impl_type_t impl_type() const { return group_impl_type_t::thread_buf_signal; }

private:
    void on_register();
    void gather_result(const bool need_latest, const counter_gather_cb_t& counter_cb, const gauge_gather_cb_t& gauge_cb,
                       const histogram_gather_cb_t& histogram_cb) override;

private:
    std::unique_ptr< PerThreadMetricsBuffer > m_metrics_buf;
    std::unique_ptr< PerThreadMetrics > m_gather_metrics;
};
} // namespace sisl
