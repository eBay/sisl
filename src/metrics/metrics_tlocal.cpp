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
#include <condition_variable>
#include <iostream>
#include <mutex>

#include "logging/logging.h"

#include "metrics_tlocal.hpp"

namespace sisl {

PerThreadMetrics::PerThreadMetrics(const std::vector< HistogramStaticInfo >& hinfo, const uint32_t ncntrs,
                                   const uint32_t nhists) :
        m_histogram_info{hinfo}, m_ncntrs{ncntrs}, m_nhists{nhists} {
    m_counters = std::make_unique< CounterValue[] >(ncntrs);
    m_histograms = std::make_unique< HistogramValue[] >(nhists);
    std::uninitialized_default_construct(m_counters.get(), m_counters.get() + ncntrs);
    std::uninitialized_default_construct(m_histograms.get(), m_histograms.get() + nhists);

#if 0
        LOG("ThreadId=%08lux: SafeMetrics=%p constructor, m_counters=%p, m_histograms=%p\n",
            pthread_self(), (void *)this, static_cast<void *>(m_counters.get()),
            static_cast<void *>(m_histograms.get()));
#endif
}

PerThreadMetrics::~PerThreadMetrics() {}

void PerThreadMetrics::merge(PerThreadMetrics* const a, PerThreadMetrics* const b) {
#if 0
        printf("ThreadId=%08lux: Merging SafeMetrics a=%p, b=%p, a->m_ncntrs=%u, b->m_ncntrs=%u, a->m_nhists=%u, b->m_nhists=%u\n",
               pthread_self(), a, b, a->m_ncntrs, b->m_ncntrs, a->m_nhists, b->m_nhists);
#endif

    for (decltype(m_ncntrs) i{0}; i < a->m_ncntrs; ++i) {
        a->m_counters[i].merge(b->m_counters[i]);
    }

    for (decltype(m_nhists) i{0}; i < a->m_nhists; ++i) {
        a->m_histograms[i].merge(b->m_histograms[i], a->m_histogram_info[i].get_boundaries());
    }
}

CounterValue& PerThreadMetrics::get_counter(const uint64_t index) {
    assert(index < m_ncntrs);
    return m_counters[index];
}

HistogramValue& PerThreadMetrics::get_histogram(const uint64_t index) {
    assert(index < m_nhists);
    return m_histograms[index];
}

/******* Thread Local Safe Metrics **********/
static int outstanding_flush{0};
static std::mutex flush_cv_mtx;
static std::condition_variable flush_cv;
static void flush_cache_handler([[maybe_unused]] sisl::logging::SignalType signal_number) {
    assert(signal_number == SIGUSR4);

    std::atomic_thread_fence(std::memory_order_release);
    {
        std::unique_lock l{flush_cv_mtx};
        --outstanding_flush;
    }
    flush_cv.notify_one();
    // std::cout << "Flushing, now outstanding flushes " << outstanding_flush << "\n";
}

ThreadBufferMetricsGroup::~ThreadBufferMetricsGroup() {}

/* We flush the cache in each thread by sending them a signal and then forcing them to do atomic barrier. Once
 * all threads run atomic barrier, notify the caller and the caller waits on a CV
 */
void ThreadBufferMetricsGroup::flush_core_cache() {
    outstanding_flush = 0;
    ThreadRegistry::instance()->foreach_running([&]([[maybe_unused]] const uint32_t thread_num, const pthread_t pt) {
        {
            std::unique_lock l{flush_cv_mtx};
            ++outstanding_flush;
        }
        // std::cout << "Sending thread signal to thread_num " << thread_num << "\n";
        sisl::logging::send_thread_signal(pt, SIGUSR4);
    });

    {
        std::unique_lock l{flush_cv_mtx};
        flush_cv.wait(l, [&] { return (outstanding_flush == 0); });
    }
    std::atomic_thread_fence(std::memory_order_acquire);
}

void ThreadBufferMetricsGroup::on_register() {
    static std::once_flag flag1;
    std::call_once(flag1, [&]() { sisl::logging::add_signal_handler(SIGUSR4, "SIGUSR4", &flush_cache_handler); });

    m_metrics_buf =
        std::make_unique< PerThreadMetricsBuffer >(m_static_info->m_histograms, num_counters(), num_histograms());
    m_gather_metrics =
        std::make_unique< PerThreadMetrics >(m_static_info->m_histograms, num_counters(), num_histograms());
}

void ThreadBufferMetricsGroup::gather_result(const bool need_latest, const counter_gather_cb_t& counter_cb,
                                             const gauge_gather_cb_t& gauge_cb,
                                             const histogram_gather_cb_t& histogram_cb) {
    if (need_latest) {
        m_gather_metrics =
            std::make_unique< PerThreadMetrics >(m_static_info->m_histograms, num_counters(), num_histograms());

        m_metrics_buf->access_all_threads([&](PerThreadMetrics* tmetrics, [[maybe_unused]] bool is_thread_running,
                                              [[maybe_unused]] bool is_last_thread) {
            PerThreadMetrics::merge(m_gather_metrics.get(), tmetrics);
            return true;
        });
    }

    for (size_t i{0}; i < num_counters(); ++i) {
        counter_cb(i, m_gather_metrics->get_counter(i));
    }

    for (size_t i{0}; i < num_gauges(); ++i) {
        gauge_cb(i, m_gauge_values[i]);
    }

    for (size_t i{0}; i < num_histograms(); ++i) {
        histogram_cb(i, m_gather_metrics->get_histogram(i));
    }
}

void ThreadBufferMetricsGroup::counter_increment(const uint64_t index, const int64_t val) {
    m_metrics_buf->get()->get_counter(index).increment(val);
}

void ThreadBufferMetricsGroup::counter_decrement(const uint64_t index, const int64_t val) {
    m_metrics_buf->get()->get_counter(index).decrement(val);
}

// If we were to call the method with count parameter and compiler inlines them, binaries linked with libsisl gets
// linker errors. At the same time we also don't want to non-inline this method, since its the most obvious call
// everyone makes and wanted to avoid additional function call in the stack. Hence we are duplicating the function
// one with count and one without count. In any case this is a single line method.
void ThreadBufferMetricsGroup::histogram_observe(const uint64_t index, const int64_t val) {
    m_metrics_buf->get()->get_histogram(index).observe(val, m_static_info->m_histograms[index].get_boundaries(), 1);
}

void ThreadBufferMetricsGroup::histogram_observe(const uint64_t index, const int64_t val, const uint64_t count) {
    m_metrics_buf->get()->get_histogram(index).observe(val, m_static_info->m_histograms[index].get_boundaries(), count);
}
} // namespace sisl
