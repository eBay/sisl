//
// Created by Kadayam, Hari on 2/5/19.
//

#include "metrics_tlocal.hpp"
#include <sds_logging/logging.h>
#include <atomic>
#include <iostream>

namespace sisl {

PerThreadMetrics::PerThreadMetrics(const std::vector< HistogramInfo >& hinfo, uint32_t ncntrs, uint32_t nhists) :
        m_histogram_info(hinfo),
        m_ncntrs(ncntrs),
        m_nhists(nhists) {
    m_counters = new CounterValue[ncntrs];
    m_histograms = new HistogramValue[nhists];

    memset((void*)m_counters, 0, (sizeof(CounterValue) * ncntrs));
    memset((void*)m_histograms, 0, (sizeof(HistogramValue) * nhists));

#if 0
        LOG("ThreadId=%08lux: SafeMetrics=%p constructor, m_counters=%p, m_histograms=%p\n",
                pthread_self(), (void *)this, (void *)m_counters, (void *)m_histograms);
#endif
}

PerThreadMetrics::~PerThreadMetrics() {
    delete[] m_counters;
    delete[] m_histograms;
}

void PerThreadMetrics::merge(PerThreadMetrics* a, PerThreadMetrics* b) {
#if 0
        printf("ThreadId=%08lux: Merging SafeMetrics a=%p, b=%p, a->m_ncntrs=%u, b->m_ncntrs=%u, a->m_nhists=%u, b->m_nhists=%u\n",
               pthread_self(), a, b, a->m_ncntrs, b->m_ncntrs, a->m_nhists, b->m_nhists);
#endif

    for (auto i = 0U; i < a->m_ncntrs; i++) {
        a->m_counters[i].merge(b->m_counters[i]);
    }

    for (auto i = 0U; i < a->m_nhists; i++) {
        a->m_histograms[i].merge(b->m_histograms[i], a->m_histogram_info[i].get_boundaries());
    }
}

CounterValue& PerThreadMetrics::get_counter(uint64_t index) {
    assert(index < m_ncntrs);
    return m_counters[index];
}

HistogramValue& PerThreadMetrics::get_histogram(uint64_t index) {
    assert(index < m_nhists);
    return m_histograms[index];
}

/******* Thread Local Safe Metrics **********/
static int _outstanding_flush = 0;
static std::mutex _flush_cv_mtx;
static std::condition_variable _flush_cv;
static void flush_cache_handler([[maybe_unused]] int signal_number, [[maybe_unused]] siginfo_t* info,
                                [[maybe_unused]] void* unused_context) {
    assert(signal_number == SIGUSR4);

    std::atomic_thread_fence(std::memory_order_release);
    {
        std::scoped_lock l(_flush_cv_mtx);
        --_outstanding_flush;
        _flush_cv.notify_one();
    }
    // std::cout << "Flushing, now outstanding flushes " << _outstanding_flush << "\n";
}

void ThreadBufferMetricsGroup::flush_core_cache() {
    _outstanding_flush = 0;
    ThreadRegistry::instance()->foreach_running([&]([[maybe_unused]] uint32_t thread_num, pthread_t pt) {
        {
            std::scoped_lock l(_flush_cv_mtx);
            ++_outstanding_flush;
        }
        // std::cout << "Sending thread signal to thread_num " << thread_num << "\n";
        sds_logging::send_thread_signal(pt, SIGUSR4);
    });

    {
        std::unique_lock l(_flush_cv_mtx);
        _flush_cv.wait(l, [&] { return (_outstanding_flush == 0); });
    }
    std::atomic_thread_fence(std::memory_order_acquire);
}

void ThreadBufferMetricsGroup::on_register() {
    static std::once_flag flag1;
    std::call_once(flag1, [&]() { sds_logging::add_signal_handler(SIGUSR4, "SIGUSR4", &flush_cache_handler); });

    m_metrics_buf = std::make_unique< PerThreadMetricsBuffer >(m_histograms, m_counters.size(), m_histograms.size());
    m_gather_metrics = std::make_unique< PerThreadMetrics >(m_histograms, m_counters.size(), m_histograms.size());
}

void ThreadBufferMetricsGroup::gather_result(
    bool need_latest, std::function< void(CounterInfo&, const CounterValue&) > counter_cb,
    std::function< void(GaugeInfo&) > gauge_cb,
    std::function< void(HistogramInfo&, const HistogramValue&) > histogram_cb) {
    if (need_latest) {
        m_gather_metrics = std::make_unique< PerThreadMetrics >(m_histograms, m_counters.size(), m_histograms.size());

        m_metrics_buf->access_all_threads([&](PerThreadMetrics* tmetrics, [[maybe_unused]] bool is_thread_running,
                                              [[maybe_unused]] bool is_last_thread) {
            PerThreadMetrics::merge(m_gather_metrics.get(), tmetrics);
            return true;
        });
    }

    for (auto i = 0u; i < m_counters.size(); ++i) {
        counter_cb(m_counters[i], m_gather_metrics->get_counter(i));
    }

    for (auto i = 0U; i < m_gauges.size(); i++) {
        gauge_cb(m_gauges[i]);
    }

    for (auto i = 0U; i < m_histograms.size(); i++) {
        histogram_cb(m_histograms[i], m_gather_metrics->get_histogram(i));
    }
}

void ThreadBufferMetricsGroup::counter_increment(uint64_t index, int64_t val) {
    m_metrics_buf->get()->get_counter(index).increment(val);
}

void ThreadBufferMetricsGroup::counter_decrement(uint64_t index, int64_t val) {
    m_metrics_buf->get()->get_counter(index).decrement(val);
}

// If we were to call the method with count parameter and compiler inlines them, binaries linked with libsisl gets
// linker errors. At the same time we also don't want to non-inline this method, since its the most obvious call
// everyone makes and wanted to avoid additional function call in the stack. Hence we are duplicating the function
// one with count and one without count. In any case this is a single line method.
void ThreadBufferMetricsGroup::histogram_observe(uint64_t index, int64_t val) {
    m_metrics_buf->get()->get_histogram(index).observe(val, m_bkt_boundaries[index], 1);
}

void ThreadBufferMetricsGroup::histogram_observe(uint64_t index, int64_t val, uint64_t count) {
    m_metrics_buf->get()->get_histogram(index).observe(val, m_bkt_boundaries[index], count);
}
} // namespace sisl