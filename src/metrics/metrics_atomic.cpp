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
#include <atomic>
#include <iostream>
#include <memory>

#include <sisl/logging/logging.h>

#include "metrics_atomic.hpp"

namespace sisl {

void AtomicMetricsGroup::on_register() {
    // std::make_unique<[]> will allocate and construct
    m_counter_values = std::make_unique< AtomicCounterValue[] >(num_counters());
    m_histogram_values = std::make_unique< AtomicHistogramValue[] >(num_histograms());
}

void AtomicMetricsGroup::gather_result([[maybe_unused]] bool need_latest, const counter_gather_cb_t& counter_cb,
                                       const gauge_gather_cb_t& gauge_cb, const histogram_gather_cb_t& histogram_cb) {
    for (size_t i{0}; i < num_counters(); ++i) {
        counter_cb(i, m_counter_values[i].to_counter_value());
    }

    for (size_t i{0}; i < num_gauges(); ++i) {
        gauge_cb(i, m_gauge_values[i]);
    }

    for (size_t i{0}; i < num_histograms(); ++i) {
        histogram_cb(i, m_histogram_values[i].to_histogram_value());
    }
}

void AtomicMetricsGroup::counter_increment(uint64_t index, int64_t val) { m_counter_values[index].increment(val); }

void AtomicMetricsGroup::counter_decrement(uint64_t index, int64_t val) { m_counter_values[index].decrement(val); }

// If we were to call the method with count parameter and compiler inlines them, binaries linked with libsisl gets
// linker errors. At the same time we also don't want to non-inline this method, since its the most obvious call
// everyone makes and wanted to avoid additional function call in the stack. Hence we are duplicating the function
// one with count and one without count. In any case this is a single line method.
void AtomicMetricsGroup::histogram_observe(uint64_t index, int64_t val) {
    m_histogram_values[index].observe(val, hist_static_info(index).get_boundaries(), 1);
}

void AtomicMetricsGroup::histogram_observe(uint64_t index, int64_t val, uint64_t count) {
    m_histogram_values[index].observe(val, hist_static_info(index).get_boundaries(), count);
}
} // namespace sisl
