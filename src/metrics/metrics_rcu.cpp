//
// Created by Kadayam, Hari on 2/5/19.
//

#include "metrics_rcu.hpp"
#include <sds_logging/logging.h>

namespace sisl {

void WisrBufferMetricsGroup::on_register() {
    m_metrics = std::make_unique< WisrBufferMetrics >(m_histograms, m_counters.size(), m_histograms.size());
}

void WisrBufferMetricsGroup::counter_increment(uint64_t index, int64_t val) {
    m_metrics->insertable_ptr()->get_counter(index).increment(val);
}

void WisrBufferMetricsGroup::counter_decrement(uint64_t index, int64_t val) {
    m_metrics->insertable_ptr()->get_counter(index).decrement(val);
}

// If we were to call the method with count parameter and compiler inlines them, binaries linked with libsisl gets
// linker errors. At the same time we also don't want to non-inline this method, since its the most obvious call
// everyone makes and wanted to avoid additional function call in the stack. Hence we are duplicating the function
// one with count and one without count. In any case this is a single line method.
void WisrBufferMetricsGroup::histogram_observe(uint64_t index, int64_t val) {
    m_metrics->insertable_ptr()->get_histogram(index).observe(val, m_bkt_boundaries[index], 1);
}

void WisrBufferMetricsGroup::histogram_observe(uint64_t index, int64_t val, uint64_t count) {
    m_metrics->insertable_ptr()->get_histogram(index).observe(val, m_bkt_boundaries[index], count);
}

void WisrBufferMetricsGroup::gather_result(bool need_latest,
                                           std::function< void(CounterInfo&, const CounterValue&) > counter_cb,
                                           std::function< void(GaugeInfo&) > gauge_cb,
                                           std::function< void(HistogramInfo&, const HistogramValue&) > histogram_cb) {
    PerThreadMetrics* tmetrics;
    if (need_latest) {
        tmetrics = m_metrics->now();
    } else {
        tmetrics = m_metrics->delayed();
    }

    for (auto i = 0U; i < m_counters.size(); i++) {
        counter_cb(m_counters[i], tmetrics->get_counter(i));
    }

    for (auto i = 0U; i < m_gauges.size(); i++) {
        gauge_cb(m_gauges[i]);
    }

    for (auto i = 0U; i < m_histograms.size(); i++) {
        histogram_cb(m_histograms[i], tmetrics->get_histogram(i));
    }
}
} // namespace sisl