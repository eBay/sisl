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

#include "histogram_buckets.hpp"
#include <atomic>
#include <array>
#include <vector>
#include <cstdint>
#include "metrics_tlocal.hpp"
#include "wisr/wisr_framework.hpp"

namespace sisl {
using WisrBufferMetrics =
    sisl::wisr_framework< PerThreadMetrics, const std::vector< HistogramStaticInfo >&, uint32_t, uint32_t >;

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
    void gather_result(bool need_latest, const counter_gather_cb_t& counter_cb, const gauge_gather_cb_t& gauge_cb,
                       const histogram_gather_cb_t& histogram_cb) override;

private:
    std::unique_ptr< WisrBufferMetrics > m_metrics;
};
} // namespace sisl
