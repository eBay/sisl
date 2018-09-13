/* Author: Sounak Gupta, July/Aug 2018 */
#pragma once

#include <vector>
#include <tuple>
#include <memory>
#include <string>
#include <cassert>
#include <chrono>
#include <atomic>
#include <algorithm>

#include "thread_buffer.hpp"
#include "nlohmann/json.hpp"

namespace metrics {

template <typename T, typename... V> constexpr auto array_of(V&&... v) -> std::array<T, sizeof...(V)> {
    return {{std::forward<T>(v)...}};
}

static auto g_histogram_bucket_specs = array_of<uint64_t>(
    300, 450, 750, 1000, 3000, 5000, 7000, 9000, 11000, 13000, 15000,
    17000, 19000, 21000, 32000, 45000, 75000, 110000, 160000, 240000,
    360000, 540000, 800000, 1200000, 1800000, 2700000, 4000000);

#define HIST_BKT_SIZE (g_histogram_bucket_specs.size() + 1)

class MetricsFactory;
extern MetricsFactory factory;

#define CREATE_REPORT   metrics::MetricsFactory factory
#define REPORT factory

class _counter {
public:
    _counter() = default;
    void init() { m_value = 0; }
    void increment (int64_t value = 1)  { m_value += value; }
    void decrement (int64_t value = 1)  { m_value -= value; }
    int64_t get() { return m_value; }
    int64_t merge (const _counter& other) {
        m_value += other.m_value;
        return m_value;
    }
private:
    int64_t m_value = 0;
};

class _gauge {
public:
    _gauge() = default;
    void init() { m_ts = 0; }
    void update (int64_t value) {
        auto ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
        if (m_ts >= ts) return;
        m_value = value;
        m_ts = ts;
    }
    int64_t get() { return m_value; }
    int64_t merge (const _gauge& other) {
        if (m_ts < other.m_ts) {
            m_value = other.m_value;
            m_ts = other.m_ts;
        }
        return m_value;
    }
private:
    int64_t m_value = 0;
    int64_t m_ts = 0;
};

class _histogram {
public:
    _histogram() { init(); }
    void init() { std::fill(m_freqs, m_freqs+HIST_BKT_SIZE, 0); }
    void update (int64_t value) {
        auto lower = std::lower_bound(g_histogram_bucket_specs.begin(),
                                    g_histogram_bucket_specs.end(), value);
        auto bkt_idx = lower - g_histogram_bucket_specs.begin();
        m_freqs[bkt_idx]++;
        m_sum += value;
    }
    void merge (const _histogram& other) {
        for (auto i = 0U; i < HIST_BKT_SIZE; i++) {
            m_freqs[i] += other.m_freqs[i];
        }
        m_sum += other.m_sum;
    }
    const int64_t* getFreqs() { return m_freqs; }
    int64_t getSum() { return m_sum; }
private:
    int64_t m_freqs[HIST_BKT_SIZE];
    int64_t m_sum = 0;
};

class SafeMetrics {
public:
    SafeMetrics() = default;
    ~SafeMetrics() {
        free(m_counters);
        free(m_gauges);
        free(m_histograms);
    }
    void init( uint64_t num_cntrs, uint64_t num_gauges, uint64_t num_hists ) {
        m_counters   = (_counter*) malloc(sizeof(_counter) * num_cntrs);
        m_gauges     = (_gauge*) malloc(sizeof(_gauge) * num_gauges);
        m_histograms = (_histogram*) malloc(sizeof(_histogram) * num_hists);

        for (auto i = 0U; i < num_cntrs; i++) {
            m_counters[i].init();
        }
        for (auto i = 0U; i < num_gauges; i++) {
            m_gauges[i].init();
        }
        for (auto i = 0U; i < num_hists; i++) {
            m_histograms[i].init();
        }
    }
    _counter* getCounter (uint64_t index) { return &m_counters[index]; }
    _gauge* getGauge (uint64_t index) { return &m_gauges[index]; }
    _histogram* getHistogram (uint64_t index) { return &m_histograms[index]; }

private:
    _counter   *m_counters;
    _gauge     *m_gauges;
    _histogram *m_histograms;
};

class Metrics {
public:
    Metrics() {}
    urcu::urcu_ptr<SafeMetrics> getSafe() { return m_safe_metrics.get(); }
    void rotate() { m_safe_metrics.make_and_exchange(); }
private:
    urcu::urcu_data<SafeMetrics> m_safe_metrics;
};

class ReportCounter {
public:
    ReportCounter(  std::string name,
                    std::string desc,
                    std::string sub_type    ) :
            m_name(name),
            m_desc(desc),
            m_sub_type(sub_type) {

        if (name != "none") {
            if (sub_type != "") {
                //m_prometheus_counter =
                //    monitor::MetricsMonitor::Instance().RegisterCounter(name, desc, {{"type", sub_type}});
            } else {
                //m_prometheus_counter = monitor::MetricsMonitor::Instance().RegisterCounter(name, desc);
            }
        }
    }

    int64_t get() { return m_counter.get(); }
    int64_t merge(_counter *other) { return m_counter.merge(*other); }
    const std::string name() { return m_name; }
    const std::string desc() { return m_desc; }
    const std::string subType() { return m_sub_type; }
    void publish() {
        //m_prometheus_counter->Update((double) m_counter.get());
    }
private:
    std::string m_name;
    std::string m_desc;
    std::string m_sub_type;
    _counter m_counter;
    //monitor::Counter *m_prometheus_counter = nullptr;
};

class ReportGauge {
public:
    ReportGauge(    std::string name,
                    std::string desc,
                    std::string sub_type    ) :
            m_name(name),
            m_desc(desc),
            m_sub_type(sub_type) {

        if (name != "none") {
            if (sub_type != "") {
                //m_prometheus_gauge =
                //    monitor::MetricsMonitor::Instance().RegisterGauge(name, desc, {{"type", sub_type}});
            } else {
                //m_prometheus_gauge = monitor::MetricsMonitor::Instance().RegisterGauge(name, desc);
            }
        }
    }

    uint64_t get() { return m_gauge.get(); };
    int64_t merge(_gauge *other) { return m_gauge.merge(*other); }
    const std::string name() { return m_name; }
    const std::string desc() { return m_desc; }
    const std::string subType() { return m_sub_type; }
    void publish() {
        //m_prometheus_gauge->Set((double) m_gauge.get());
    }
private:
    const std::string m_name, m_desc, m_sub_type;
    _gauge m_gauge;
    //monitor::Gauge *m_prometheus_gauge;
};

class ReportHistogram {
public:
    ReportHistogram(std::string name,
                    std::string desc,
                    std::string sub_type    ) :
            m_name(name),
            m_desc(desc),
            m_sub_type(sub_type) {
 
        if (name != "none") {
            if (sub_type != "") {
                //m_prometheus_hist =
                //    monitor::MetricsMonitor::Instance().RegisterHistogram(name, desc, {{"type", sub_type}});
            } else {
                //m_prometheus_hist = monitor::MetricsMonitor::Instance().RegisterHistogram(name, desc);
            }
        }
    }
    double percentile(float pcntl) {
        std::array<int64_t, HIST_BKT_SIZE> cum_freq;
        int64_t fcount = 0;
        for (auto i = 0U; i < HIST_BKT_SIZE; i++) {
            fcount += (m_histogram.getFreqs())[i];
            cum_freq[i] = fcount;
        }

        int64_t pnum = fcount * pcntl/100;
        auto i = (std::lower_bound(cum_freq.begin(), cum_freq.end(), pnum)) -
                                                                cum_freq.begin();
        if ( (m_histogram.getFreqs())[i] == 0 ) return 0;
        auto Yl = i == 0 ? 0 : g_histogram_bucket_specs[i-1];
        auto ith_cum_freq = (i == 0) ? 0 : cum_freq[i-1];
        double Yp = Yl + (((pnum - ith_cum_freq) * i)/(m_histogram.getFreqs())[i]);
        return Yp;

        /* Formula:
            Yp = lower bound of i-th bucket + ((pn - cumfreq[i-1]) * i ) / freq[i]
            where
                pn = (cnt * percentile)/100
                i  = matched index of pnum in cum_freq
         */
    }
    int64_t count() {
        int64_t cnt = 0;
        for (auto i = 0U; i < HIST_BKT_SIZE; i++) {
            cnt += (m_histogram.getFreqs())[i];
        }
        return cnt;
    }
    double average() {
        auto cnt = count();
        return (cnt ? m_histogram.getSum()/cnt : 0);
    }
    void merge(_histogram *other) { m_histogram.merge(*other); }
    const std::string name() { return m_name; }
    const std::string desc() { return m_desc; }
    const std::string subType() { return m_sub_type; }
    void publish() {
        //std::vector<double> vec(std::begin(m_histogram.getFreqs()), std::end(m_histogram.getFreqs()));
        //m_prometheus_hist->Update(vec, m_histogram.m_sum);
    }
private:
    const std::string m_name, m_desc, m_sub_type;
    _histogram m_histogram;
   //monitor::Histogram *m_prometheus_hist;
};

class MetricsResult;
class MetricsFactory {
public:
    MetricsFactory() = default;
    uint64_t registerCounter(std::string name, std::string desc, std::string sub_type) {
        m_counters.emplace_back(name, desc, sub_type);
        return m_counters.size()-1;
    }
    uint64_t registerGauge(std::string name, std::string desc, std::string sub_type) {
        m_gauges.emplace_back(name, desc, sub_type);
        return m_gauges.size()-1;
    }
    uint64_t registerHistogram(std::string name, std::string desc, std::string sub_type) {
        m_histograms.emplace_back(name, desc, sub_type);
        return m_histograms.size()-1;
    }
    void startMetrics() {
        m_buffer->getSafe()->init(
            m_counters.size(), m_gauges.size(), m_histograms.size() );
    }
    _counter *getCounter(uint64_t index) { return m_buffer->getSafe()->getCounter(index); }
    _gauge *getGauge(uint64_t index) { return m_buffer->getSafe()->getGauge(index); }
    _histogram *getHistogram(uint64_t index) { return m_buffer->getSafe()->getHistogram(index); }
    std::unique_ptr<MetricsResult> gather() {
        return std::make_unique<MetricsResult>(this, m_buffer);
    }

    std::vector<ReportCounter>      m_counters;
    std::vector<ReportGauge>        m_gauges;
    std::vector<ReportHistogram>    m_histograms;
    fds::ThreadBuffer<Metrics>      m_buffer;
};

class MetricsResult {
public:
    MetricsResult(MetricsFactory* factory, fds::ThreadBuffer<Metrics>& all_buf) {
        m_factory = factory;
        all_buf.access_all_threads([factory](Metrics *m) {
            /* get current metrics instance */
            auto metrics = m->getSafe();
            for (auto i = 0U; i < factory->m_counters.size(); i++) {
                factory->m_counters[i].merge(metrics->getCounter(i));
            }
            for (auto i = 0U; i < factory->m_gauges.size(); i++) {
                factory->m_gauges[i].merge(metrics->getGauge(i));
            }
            for (auto i = 0U; i < factory->m_histograms.size(); i++) {
                factory->m_histograms[i].merge(metrics->getHistogram(i));
            }
            /* replace new metrics instance */
            m->rotate();
            m->getSafe()->init( factory->m_counters.size(),
                                factory->m_gauges.size(),
                                factory->m_histograms.size() );
        });
    }

    ~MetricsResult() { urcu::urcu_ctl::declare_quiscent_state(); }

    void publish() {
        for (auto i = 0U; i < m_factory->m_counters.size(); i++) {
            m_factory->m_counters[i].publish();
        }
        for (auto i = 0U; i < m_factory->m_gauges.size(); i++) {
            m_factory->m_gauges[i].publish();
        }
        for (auto i = 0U; i < m_factory->m_histograms.size(); i++) {
            m_factory->m_histograms[i].publish();
        }
    }

    std::string getJSON() const {
        nlohmann::json json;
        nlohmann::json counter_entries;
        for (auto &c : m_factory->m_counters) {
            std::string desc = c.desc() + " - " + c.subType();
            counter_entries[desc] = c.get();
        }
        json["Counters"] = counter_entries;

        nlohmann::json gauge_entries;
        for (auto &g : m_factory->m_gauges) {
            std::string desc = g.desc() + " - " + g.subType();
            gauge_entries[desc] = g.get();
        }
        json["Gauges"] = gauge_entries;

        nlohmann::json hist_entries;
        for (auto &h : m_factory->m_histograms) {
            std::stringstream ss;
            ss << h.average() << " / " << h.percentile(50) << " / " << h.percentile(95)
                                << " / " << h.percentile(99);
            std::string desc = h.desc() + " - " + h.subType();
            hist_entries[desc] = ss.str();
        }
        json["Histograms percentiles (usecs) avg/50/95/99"] = hist_entries;

        return json.dump();
    }

private:
    MetricsFactory* m_factory;
};

}
