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
#include <mutex>
#include <string>
#include <benchmark/benchmark.h>
#include <boost/preprocessor/repetition/repeat.hpp>

#include "sisl/metrics/metrics.hpp"

SISL_LOGGING_INIT(vmod_metrics_framework)
RCU_REGISTER_INIT

#define ITERATIONS 1000
#define THREADS 4

using namespace sisl;

#define NGAUGES 50
#define NCOUNTERS 50
#define NHISTOGRAMS 50

typedef std::pair< std::vector< double >, int64_t > hist_result_t;

struct _locked_hist_wrapper {
    _locked_hist_wrapper() : m_hist("hist", "Sample histogram", "", {"", ""}, HistogramBucketsType(DefaultBuckets)) {}

    void observe(uint64_t value) {
        std::lock_guard< std::mutex > g(m_lock);
        m_value.observe(value, HistogramBucketsType(DefaultBuckets));
    }

    hist_result_t get_result() {
        std::lock_guard< std::mutex > g(m_lock);
        std::vector< double > vec(std::begin(m_value.get_freqs()), std::end(m_value.get_freqs()));
        return std::make_pair(vec, m_value.get_sum());
    }

    std::mutex m_lock;
    HistogramStaticInfo m_hist;
    HistogramValue m_value;
};

struct atomic_counter_groups {
    std::array< std::atomic< uint64_t >, NCOUNTERS > m_counters;
    std::array< std::atomic< uint64_t >, NGAUGES > m_gauges;
    std::array< _locked_hist_wrapper, NHISTOGRAMS > m_histograms;

    atomic_counter_groups() {
        for (auto i = 0; i < NCOUNTERS; i++) {
            m_counters[i].store(0, std::memory_order_relaxed);
        }

        for (auto i = 0; i < NGAUGES; i++) {
            m_gauges[i].store(0, std::memory_order_relaxed);
        }
    }
    std::atomic< uint64_t >& getCounter(int index) { return m_counters[index]; }

    std::atomic< uint64_t >& getGauge(int index) { return m_gauges[index]; }

    void updateHist(int index, uint64_t value) { m_histograms[index].observe(value); }

    std::vector< hist_result_t > hist_results() {
        std::vector< hist_result_t > res;
        res.reserve(NCOUNTERS);

        for (auto i = 0; i < NCOUNTERS; i++) {
            res.emplace_back(m_histograms[i].get_result());
        }
        return res;
    }
};

atomic_counter_groups glob_matomic_grp;

#define DIRECT_METRICS 1

#ifdef DIRECT_METRICS
MetricsGroupImplPtr glob_tbuffer_mgroup;
MetricsGroupImplPtr glob_rcu_mgroup;
MetricsGroupImplPtr glob_atomic_mgroup;

void setup() {
    // Initialize rcu based metric group
    glob_tbuffer_mgroup = MetricsGroup::make_group("Group1", "Instance1", group_impl_type_t::thread_buf_signal);
    glob_rcu_mgroup = MetricsGroup::make_group("Group2", "Instance1", group_impl_type_t::rcu);
    glob_atomic_mgroup = MetricsGroup::make_group("Group3", "Instance1", group_impl_type_t::atomic);

    for (auto i = 0; i < NCOUNTERS; i++) {
        std::stringstream ss;
        ss << "counter" << i + 1;
        glob_tbuffer_mgroup->register_counter(ss.str(), " for test", "");
        glob_rcu_mgroup->register_counter(ss.str(), " for test", "");
        glob_atomic_mgroup->register_counter(ss.str(), " for test", "");
    }

    for (auto i = 0; i < NGAUGES; i++) {
        std::stringstream ss;
        ss << "gauge" << i + 1;
        glob_tbuffer_mgroup->register_gauge(ss.str(), " for test", "");
        glob_rcu_mgroup->register_gauge(ss.str(), " for test", "");
        glob_atomic_mgroup->register_gauge(ss.str(), " for test", "");
    }

    for (auto i = 0; i < NHISTOGRAMS; i++) {
        std::stringstream ss;
        ss << "histogram" << i + 1;
        glob_tbuffer_mgroup->register_histogram(ss.str(), " for test", "");
        glob_rcu_mgroup->register_histogram(ss.str(), " for test", "");
        glob_atomic_mgroup->register_histogram(ss.str(), " for test", "");
    }

    MetricsFarm::getInstance().register_metrics_group(glob_tbuffer_mgroup);
    MetricsFarm::getInstance().register_metrics_group(glob_rcu_mgroup);
    MetricsFarm::getInstance().register_metrics_group(glob_atomic_mgroup);
}

void teardown() {
    MetricsFarm::getInstance().deregister_metrics_group(glob_tbuffer_mgroup);
    MetricsFarm::getInstance().deregister_metrics_group(glob_rcu_mgroup);
    MetricsFarm::getInstance().deregister_metrics_group(glob_atomic_mgroup);
    glob_tbuffer_mgroup.reset();
    glob_rcu_mgroup.reset();
    glob_atomic_mgroup.reset();
}

void test_counters_write_tbuffer(benchmark::State& state) {
    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        for (auto i = 0; i < NCOUNTERS; i++) {
            glob_tbuffer_mgroup->counter_increment(i);
        }
    }
}

void test_counters_write_rcu(benchmark::State& state) {
    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        for (auto i = 0; i < NCOUNTERS; i++) {
            glob_rcu_mgroup->counter_increment(i);
        }
    }
}

void test_gauge_write_tbuffer(benchmark::State& state) {
    auto v = 1U;
    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        for (auto i = 0; i < NGAUGES; i++) {
            glob_tbuffer_mgroup->gauge_update(i, v * (i + 1));
        }
        ++v;
    }
}

void test_counters_write_atomic(benchmark::State& state) {
    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        for (auto i = 0; i < NCOUNTERS; i++) {
            glob_atomic_mgroup->counter_increment(i);
        }
    }
}

void test_histogram_write_tbuffer(benchmark::State& state) {
    auto v = 1U;

    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        for (auto i = 0; i < NHISTOGRAMS; i++) {
            glob_tbuffer_mgroup->histogram_observe(i, v * (i + 1));
        }
        ++v;
    }
}

void test_histogram_write_rcu(benchmark::State& state) {
    auto v = 1U;

    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        for (auto i = 0; i < NHISTOGRAMS; i++) {
            glob_rcu_mgroup->histogram_observe(i, v * (i + 1));
        }
        ++v;
    }
}

void test_histogram_write_atomic(benchmark::State& state) {
    auto v = 1U;

    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        for (auto i = 0; i < NHISTOGRAMS; i++) {
            glob_atomic_mgroup->histogram_observe(i, v * (i + 1));
        }
        ++v;
    }
}
#else

using namespace metrics;

class GlobMetrics : public MetricsGroupWrapper {
#define _REG_COUNTER(z, n, d) REGISTER_COUNTER(BOOST_PP_CAT(mycounter, n), "Test Counter", "");
#define _REG_GAUGE(z, n, d) REGISTER_GAUGE(BOOST_PP_CAT(mygauge, n), "Test Gauge", "");
#define _REG_HISTOGRAM(z, n, d) REGISTER_HISTOGRAM(BOOST_PP_CAT(myhist, n), "Test Histogram", "");

public:
    GlobMetrics() {
        BOOST_PP_REPEAT(NCOUNTERS, _REG_COUNTER, );
        BOOST_PP_REPEAT(NGAUGES, _REG_GAUGE, );
        BOOST_PP_REPEAT(NHISTOGRAMS, _REG_HISTOGRAM, );

        register_me_to_farm();
    }
};

GlobMetrics glob_metrics;
void setup() {}

#define _INC_COUNTER(z, n, d) COUNTER_INCREMENT(glob_metrics, BOOST_PP_CAT(mycounter, n), 1);
#define _UPD_GAUGE(z, n, d) GAUGE_UPDATE(glob_metrics, BOOST_PP_CAT(mygauge, n), (d * (n + 1)));
#define _OBS_HISTOGRAM(z, n, d) HISTOGRAM_OBSERVE(glob_metrics, BOOST_PP_CAT(myhist, n), (d * (n + 1)));

void test_counters_write_rcu(benchmark::State& state) {
    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        BOOST_PP_REPEAT(NCOUNTERS, _INC_COUNTER, );
    }
}

void test_gauge_write_rcu(benchmark::State& state) {
    auto v = 1U;
    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        BOOST_PP_REPEAT(NGAUGES, _UPD_GAUGE, v);
        ++v;
    }
}

void test_histogram_write_rcu(benchmark::State& state) {
    auto v = 1U;

    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        BOOST_PP_REPEAT(NHISTOGRAMS, _OBS_HISTOGRAM, v);
        ++v;
    }
}
#endif

void test_metrics_read_atomic(benchmark::State& state) {
    std::string str;
    // Actual test
    // Loops upto iteration count
    for (auto _ : state) {
        // benchmark::DoNotOptimize(str = MetricsFarm::getInstance().get_result_in_json_string());
        // MetricsFarm::getInstance().gather();
        glob_atomic_mgroup->gather();
    }

    // std::cout << "str = " << str << "\n";
}

void test_metrics_read_tbuffer(benchmark::State& state) {
    std::string str;
    // Actual test
    // Loops upto iteration count
    for (auto _ : state) {
        // benchmark::DoNotOptimize(str = MetricsFarm::getInstance().get_result_in_json_string());
        // MetricsFarm::getInstance().gather();
        glob_tbuffer_mgroup->gather();
    }

    // std::cout << "str = " << str << "\n";
}

void test_metrics_read_rcu(benchmark::State& state) {
    std::string str;
    // Actual test
    // Loops upto iteration count
    for (auto _ : state) {
        // benchmark::DoNotOptimize(str = MetricsFarm::getInstance().get_result_in_json_string());
        // MetricsFarm::getInstance().gather();
        glob_rcu_mgroup->gather();
    }

    // std::cout << "str = " << str << "\n";
}

#if 0
void test_counters_write_atomic(benchmark::State& state) {
    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        for (auto i = 0; i < NCOUNTERS; i++) {
            glob_matomic_grp.getCounter(i).fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void test_gauge_write_atomic(benchmark::State& state) {
    auto v = 1U;

    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        for (auto i = 0; i < NGAUGES; i++) {
            glob_matomic_grp.getGauge(i).store(v * (i + 1), std::memory_order_relaxed);
        }
        ++v;
    }
}
#endif

void test_histogram_write_locked(benchmark::State& state) {
    auto v = 1U;

    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        for (auto i = 0; i < NHISTOGRAMS; i++) {
            glob_matomic_grp.updateHist(i, v * (i + 1));
        }
        ++v;
    }
}

void test_counters_read_atomic(benchmark::State& state) {
    auto val = 0;
    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        for (auto i = 0; i < NCOUNTERS; i++) {
            val += glob_matomic_grp.getCounter(i).load(std::memory_order_relaxed);
        }
    }
}

void test_gauge_read_atomic(benchmark::State& state) {
    uint64_t val = 0;
    // Actual test
    for (auto _ : state) { // Loops upto iteration count
        for (auto i = 0; i < NGAUGES; i++) {
            val += glob_matomic_grp.getGauge(i).load(std::memory_order_relaxed);
        }
    }
}

void test_histogram_read_locked(benchmark::State& state) {
    // Actual test
    std::vector< hist_result_t > res;
    for (auto _ : state) { // Loops upto iteration count
        res = glob_matomic_grp.hist_results();
        // benchmark::DoNotOptimize(res = glob_matomic_grp.hist_results());
    }
}

BENCHMARK(test_counters_write_atomic)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_counters_write_rcu)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_counters_write_tbuffer)->Iterations(ITERATIONS)->Threads(THREADS);

// BENCHMARK(test_gauge_write_atomic)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_gauge_write_tbuffer)->Iterations(ITERATIONS)->Threads(THREADS);

// BENCHMARK(test_histogram_write_locked)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_histogram_write_atomic)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_histogram_write_rcu)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_histogram_write_tbuffer)->Iterations(ITERATIONS)->Threads(THREADS);

// BENCHMARK(test_gauge_read_atomic)->Iterations(ITERATIONS)->Threads(1);
// BENCHMARK(test_histogram_read_locked)->Iterations(ITERATIONS)->Threads(1);
BENCHMARK(test_metrics_read_atomic)->Iterations(ITERATIONS)->Threads(1);
BENCHMARK(test_metrics_read_tbuffer)->Iterations(ITERATIONS)->Threads(1);
BENCHMARK(test_metrics_read_rcu)->Iterations(ITERATIONS)->Threads(1);

int main(int argc, char** argv) {
    setup();
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    teardown();
}
