//
// Created by Kadayam, Hari on 2/5/19.
//

#include "metrics_atomic.hpp"
#include <sds_logging/logging.h>
#include <atomic>
#include <iostream>

namespace sisl {

void AtomicMetricsGroup::on_register() {
    m_counter_values = new AtomicCounterValue[m_counters.size()];
    m_histogram_values = new AtomicHistogramValue[m_histograms.size()];

    memset((void*)m_counter_values, 0, (sizeof(AtomicCounterValue) * m_counters.size()));
    memset((void*)m_histogram_values, 0, (sizeof(AtomicHistogramValue) * m_histograms.size()));
}

AtomicMetricsGroup::~AtomicMetricsGroup() {
    if (m_counter_values) delete[] m_counter_values;
    if (m_histogram_values) delete[] m_histogram_values;
}

void AtomicMetricsGroup::gather_result([[maybe_unused]] bool need_latest,
                                       std::function< void(CounterInfo&, const CounterValue&) > counter_cb,
                                       std::function< void(GaugeInfo&) > gauge_cb,
                                       std::function< void(HistogramInfo&, const HistogramValue&) > histogram_cb) {
    for (auto i = 0u; i < m_counters.size(); ++i) {
        counter_cb(m_counters[i], m_counter_values[i].to_counter_value());
    }

    for (auto i = 0U; i < m_gauges.size(); i++) {
        gauge_cb(m_gauges[i]);
    }

    for (auto i = 0U; i < m_histograms.size(); i++) {
        histogram_cb(m_histograms[i], m_histogram_values[i].to_histogram_value());
    }
}

void AtomicMetricsGroup::counter_increment(uint64_t index, int64_t val) { m_counter_values[index].increment(val); }

void AtomicMetricsGroup::counter_decrement(uint64_t index, int64_t val) { m_counter_values[index].decrement(val); }

// If we were to call the method with count parameter and compiler inlines them, binaries linked with libsisl gets
// linker errors. At the same time we also don't want to non-inline this method, since its the most obvious call
// everyone makes and wanted to avoid additional function call in the stack. Hence we are duplicating the function
// one with count and one without count. In any case this is a single line method.
void AtomicMetricsGroup::histogram_observe(uint64_t index, int64_t val) {
    m_histogram_values[index].observe(val, m_bkt_boundaries[index], 1);
}

void AtomicMetricsGroup::histogram_observe(uint64_t index, int64_t val, uint64_t count) {
    m_histogram_values[index].observe(val, m_bkt_boundaries[index], count);
}
} // namespace sisl