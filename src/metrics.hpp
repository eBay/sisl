//
// Created by Gupta, Sounak on 07/15/18.
//
#pragma once

#include <vector>
#include <tuple>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <cassert>
#include <chrono>
#include <atomic>

#include "libutils/fds/thread/thread_buffer.hpp"
#include "libutils/fds/utility/urcu_helper.hpp"
#include "monitor/include/metrics_monitor.hpp"

namespace metrics {

class _counter {
public:
    explicit _counter(int64_t init_value) : m_value(init_value) {}

    void increment(int64_t value = 1)  { m_value += value; }

    void decrement(int64_t value = 1)  { m_value -= value; }

    int64_t get() { return m_value; }

    int64_t merge(const _counter &other) {
        m_value += other.m_value;
        return m_value;
    }

private:
    int64_t m_value;
};

class _gauge {
public:
    explicit _gauge(int64_t init_value) : m_value(init_value) {}

    void update(int64_t value) {
        auto ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
        if (m_ts >= ts) return;
        m_value = value;
        m_ts = ts;
    }

    int64_t get() { return m_value; }

    int64_t merge(const _gauge& other) {
        if (m_ts < other.m_ts) {
            m_value = other.m_value;
            m_ts = other.m_ts;
        }
        return m_value;
    }

private:
    int64_t m_value;
    int64_t m_ts = 0;
};

class _histogram {
public:
    _histogram(std::vector<uint64_t> buckets) : m_buckets(buckets) {
        m_freqs.assign(buckets.size() + 1, 0);
    }

    void observe(int64_t value) {
        auto lower = std::lower_bound(m_buckets.begin(), m_buckets.end(), value);
        auto bkt_idx = lower - m_buckets.begin();
        m_freqs[bkt_idx]++;
        m_sum += value;
    }

    void merge(const _histogram& other) {
        for (auto i = 0U; i < other.m_freqs.size(); i++) {
            m_freqs[i] += other.m_freqs[i];
        }
        m_sum += other.m_sum;
    }

    const std::vector<uint64_t> getBuckets() const { return m_buckets; }

    std::vector<int64_t> getFreqs() { return m_freqs; }

    int64_t getSum() { return m_sum; }

private:
    std::vector<int64_t>    m_freqs;
    std::vector<uint64_t>   m_buckets;
    int64_t                 m_sum = 0;
};

class Metrics {
public:
    Metrics() = default;

    void registerCounter(int64_t init_val ) { m_counters.emplace_back(init_val); }

    void registerGauge(int64_t init_val) { m_gauges.emplace_back(init_val); }

    void registerHistogram(std::vector<uint64_t> buckets) { m_histograms.emplace_back(buckets); }

    _counter fetchCounter(uint64_t index) {
        assert(index < m_counters.size());
        return m_counters[index];
    }

    _gauge fetchGauge(uint64_t index) {
        assert(index < m_gauges.size());
        return m_gauges[index];
    }

    _histogram fetchHistogram(uint64_t index) {
        assert(index < m_histograms.size());
        return m_histograms[index];
    }

private:
    std::vector<_counter>   m_counters;
    std::vector<_gauge>     m_gauges;
    std::vector<_histogram> m_histograms;
};

class MetricsController {
public:
    MetricsController() = default;
    urcu_ptr<Metrics> fetchMetrics() { return m_metrics_data.get(); }

    void swap() { m_metrics_data.make_and_exchange(); }

private:
    urcu_data<Metrics> m_metrics_data;
};

class ReportCounter {
public:
    ReportCounter(  std::string name,
                    std::string desc,
                    std::string sub_type,
                    int64_t init_val    ) :
            m_name(name),
            m_desc(desc),
            m_sub_type(sub_type),
            m_counter(init_val) {

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

    int64_t merge(const _counter &other) { return m_counter.merge(other); }

    const std::string getName() { return m_name; }

    const std::string getDesc() { return m_desc; }

    const std::string getSubType() { return m_sub_type; }

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
                    std::string sub_type,
                    int64_t init_val    ) :
            m_name(name),
            m_desc(desc),
            m_sub_type(sub_type),
            m_gauge(init_val) {

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

    int64_t merge(const _gauge &other) { return m_gauge.merge(other); }

    const std::string getName() { return m_name; }

    const std::string getDesc() { return m_desc; }

    const std::string getSubType() { return m_sub_type; }

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
                    std::string sub_type,
                    std::vector<uint64_t> buckets   ) :
            m_name(name),
            m_desc(desc),
            m_sub_type(sub_type),
            m_histogram(buckets) {

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
        auto freqs = m_histogram.getFreqs();
        std::vector<int64_t> cum_freq(0, freqs.size());
        int64_t fcount = 0;
        for (auto i = 0U; i < freqs.size(); i++) {
            fcount += freqs[i];
            cum_freq[i] = fcount;
        }

        /* Formula:
            Yp = lower bound of i-th bucket + ( ( (pn - cumfreq[i-1]) * i )/freq[i] )
            where
                pn = fcount * percentile/100
                i  = matched index of pnum in cum_freq
        */
        int64_t pnum = fcount * pcntl/100;
        auto i = (std::lower_bound(cum_freq.begin(), cum_freq.end(), pnum)) - cum_freq.begin();
        if (!i || !freqs[i]) return 0;
        return (m_histogram.getBuckets())[i-1] + ((pnum - cum_freq[i-1]) * i) / freqs[i];
    }

    double average() {
        int64_t fcount = 0;
        for (auto freq : m_histogram.getFreqs()) { fcount += freq; }
        return (fcount ? m_histogram.getSum()/fcount : 0);
    }

    void merge(const _histogram &other) { m_histogram.merge(other); }

    const std::string getName() { return m_name; }

    const std::string getDesc() { return m_desc; }

    const std::string getSubType() { return m_sub_type; }

    void publish() {
        std::vector<double> vec(std::begin(m_histogram.getFreqs()), std::end(m_histogram.getFreqs()));
        //m_prometheus_hist->Update(vec, m_histogram.m_sum);
    }

private:
    const std::string m_name, m_desc, m_sub_type;
    _histogram m_histogram;
   //monitor::Histogram *m_prometheus_hist;
};

class ReportMetrics {
public:
    ReportMetrics() = default;

    uint64_t registerCounter(std::string name, std::string desc, std::string sub_type, int64_t init_val) {
        m_counters.emplace_back(name, desc, sub_type, init_val);
        m_buffer.get()->fetchMetrics()->registerCounter(init_val);
        return m_counters.size()-1;
    }
 
    uint64_t registerGauge(std::string name, std::string desc, std::string sub_type, int64_t init_val) {
        m_gauges.emplace_back(name, desc, sub_type, init_val);
        m_buffer.get()->fetchMetrics()->registerGauge(init_val);
        return m_gauges.size()-1;
    }
 
    uint64_t registerHistogram(std::string name, std::string desc, std::string sub_type,
            std::vector<uint64_t> buckets =
                {   300,    450,    750,    1000,   3000,   5000,    7000,    9000,    11000,
                    13000,  15000,  17000,  19000,  21000,  32000,   45000,   75000,   110000,
                    160000, 240000, 360000, 540000, 800000, 1200000, 1800000, 2700000, 4000000  } ) {
        m_histograms.emplace_back(name, desc, sub_type, buckets);
        m_buffer.get()->fetchMetrics()->registerHistogram(buckets);
        return m_histograms.size()-1;
    }

    _counter fetchCounter(uint64_t index) { return m_buffer.get()->fetchMetrics()->fetchCounter(index); }

    _gauge fetchGauge(uint64_t index) { return m_buffer.get()->fetchMetrics()->fetchGauge(index); }

    _histogram fetchHistogram(uint64_t index) { return m_buffer.get()->fetchMetrics()->fetchHistogram(index); }

    void gather() {
        for (auto i = 0U; i < m_counters.size(); i++) {
            m_counters[i].merge(fetchCounter(i));
        }

        for (auto i = 0U; i < m_gauges.size(); i++) {
            m_gauges[i].merge(fetchGauge(i));
        }

        for (auto i = 0U; i < m_histograms.size(); i++) {
            m_histograms[i].merge(fetchHistogram(i));
        }

        /* replace new metrics instance */
        m_buffer.get()->swap();
    }

    std::string get_json() {
        nlohmann::json json;
        nlohmann::json counter_entries;
        for (auto i : m_counters) {
            auto desc = i.getDesc();
            if (i.getSubType() != "") desc += " - " + i.getSubType();
            counter_entries[desc] = i.get();
        }
        json["Counters"] = counter_entries;

        nlohmann::json gauge_entries;
        for (auto i : m_gauges) {
            std::string desc = i.getDesc();
            if (i.getSubType() != "") desc += " - " + i.getSubType();
            gauge_entries[desc] = i.get();
        }
        json["Gauges"] = gauge_entries;

        nlohmann::json hist_entries;
        for (auto i : m_histograms) {
            std::stringstream ss;
            ss  << i.average()      << " / "
                << i.percentile(50) << " / "
                << i.percentile(95) << " / "
                << i.percentile(99);

            std::string desc = i.getDesc();
            if (i.getSubType() != "") desc += " - " + i.getSubType();
            hist_entries[desc] = ss.str();
        }
        json["Histograms percentiles (usecs) avg/50/95/99"] = hist_entries;

        return json.dump();
    }

    void publish() {
        for (auto i : m_counters) { i.publish(); }
        for (auto i : m_gauges) { i.publish(); }
        for (auto i : m_histograms) { i.publish(); }
    }

private:
    std::vector<ReportCounter>              m_counters;
    std::vector<ReportGauge>                m_gauges;
    std::vector<ReportHistogram>            m_histograms;
    fds::ThreadBuffer<MetricsController>    m_buffer;
};

}
