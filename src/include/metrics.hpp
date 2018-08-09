//
// Created by Gupta, Sounak on 07/15/18.
//
#pragma once

#include <vector>
#include <tuple>
#include <memory>
#include <string>
#include <cassert>
#include <chrono>
#include <atomic>

#include "include/urcu_helper.hpp"
#include "nlohmann/json.hpp"

namespace metrics {

#define ARR_BLOCK 8

class _counter {
public:
    _counter() = default;

    void init (int64_t value) { m_value = value; }

    void increment (int64_t value = 1)  { m_value += value; }

    void decrement (int64_t value = 1)  { m_value -= value; }

    int64_t get() { return m_value; }

    int64_t merge (const _counter &other) {
        m_value += other.m_value;
        return m_value;
    }

private:
    int64_t m_value = 0;
};

class _gauge {
public:
    _gauge() = default;

    void init (int64_t value) { m_value = value; }

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
    uint64_t m_ts = 0;
};

class _histogram {
public:
    _histogram() = default;

    void init ( std::vector<uint64_t> buckets ) {
        m_bucket_cnt = buckets.size();
        m_buckets = new uint64_t[m_bucket_cnt];
        m_freqs = new int64_t[m_bucket_cnt+1];
        for (auto i = 0U; i < m_bucket_cnt; i++) {
            m_buckets[i] = buckets[i];
            m_freqs[i] = 0;
        }
        m_freqs[m_bucket_cnt] = 0;
    }

    void update (int64_t value) {
        unsigned int index = 0;
        while ( index < m_bucket_cnt && m_buckets[index] < value ) index++;
        m_freqs[index]++;
        m_sum += value;
    }

    void merge (const _histogram& other) {
        for (auto i = 0U; i < other.m_bucket_cnt+1; i++) {
            m_freqs[i] += other.m_freqs[i];
        }
        m_sum += other.m_sum;
    }

    const uint64_t* getBuckets() { return m_buckets; }

    const int64_t* getFreqs() { return m_freqs; }

    void resetFreqs() {
        memset(m_freqs, 0, m_bucket_cnt+1);
        m_sum = 0;
    }

    uint64_t getBucketCnt() { return m_bucket_cnt; }

    int64_t getSum() { return m_sum; }

private:
    int64_t*    m_freqs = nullptr;
    uint64_t*   m_buckets = nullptr;
    uint64_t    m_bucket_cnt = 0;
    int64_t     m_sum = 0;
};

class Metrics {
public:
    Metrics() {
        m_counters   = new _counter[ARR_BLOCK];
        m_gauges     = new _gauge[ARR_BLOCK];
        m_histograms = new _histogram[ARR_BLOCK];
    }

    void addCounter(int64_t init_val ) {
        /* If size extension is needed */
        if ( m_counter_cnt && m_counter_cnt % ARR_BLOCK == 0 ) {
            auto temp = (_counter *) realloc(m_counters, (m_counter_cnt + ARR_BLOCK) * sizeof(_counter));
            assert(temp);
            m_counters = temp;
        }
        m_counters[m_counter_cnt++].init(init_val);
    }

    void addGauge(int64_t init_val) {
        /* If size extension is needed */
        if ( m_gauge_cnt && m_gauge_cnt % ARR_BLOCK == 0 ) {
            auto temp = (_gauge *) realloc(m_gauges, (m_gauge_cnt + ARR_BLOCK) * sizeof(_gauge));
            assert(temp);
            m_gauges = temp;
        }
        m_gauges[m_gauge_cnt++].init(init_val);
    }

    void addHistogram(std::vector<uint64_t> buckets) {
        /* If size extension is needed */
        if ( m_histogram_cnt && m_histogram_cnt % ARR_BLOCK == 0 ) {
            auto temp = (_histogram *) realloc(m_histograms, (m_histogram_cnt + ARR_BLOCK) * sizeof(_histogram));
            assert(temp);
            m_histograms = temp;
        }
        m_histograms[m_histogram_cnt++].init(buckets);
    }

    _counter* fetchCounter (uint64_t index) {
        assert(index < m_counter_cnt);
        return &m_counters[index];
    }

    _gauge* fetchGauge (uint64_t index) {
        assert(index < m_gauge_cnt);
        return &m_gauges[index];
    }

    _histogram* fetchHistogram(uint64_t index) {
        assert(index < m_histogram_cnt);
        return &m_histograms[index];
    }

    uint64_t numCounters() { return m_counter_cnt; }

    uint64_t numGauges() { return m_gauge_cnt; }

    uint64_t numHistograms() { return m_histogram_cnt; }

private:
    _counter*   m_counters = nullptr;
    uint64_t    m_counter_cnt = 0;

    _gauge*     m_gauges = nullptr;
    uint64_t    m_gauge_cnt = 0;

    _histogram* m_histograms = nullptr;
    uint64_t    m_histogram_cnt = 0;
};

class MetricsController {
public:
    MetricsController() {}

    urcu::urcu_ptr<Metrics> fetchMetrics() { return m_metrics_data.get(); }

    void swap() { m_metrics_data.make_and_exchange(); }

private:
    urcu::urcu_data<Metrics> m_metrics_data;
};

class ReportCounter {
public:
    ReportCounter(  std::string name,
                    std::string desc,
                    std::string sub_type,
                    int64_t init_val    ) :
            m_name(name),
            m_desc(desc),
            m_sub_type(sub_type) {

        m_counter.init(init_val);

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

    void reset() { m_counter.init(0); }

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
            m_sub_type(sub_type) {

        m_gauge.init(init_val);

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
            m_sub_type(sub_type) {
 
        m_histogram.init(buckets);

        if (name != "none") {
            if (sub_type != "") {
                //m_prometheus_hist =
                //    monitor::MetricsMonitor::Instance().RegisterHistogram(name, desc, {{"type", sub_type}});
            } else {
                //m_prometheus_hist = monitor::MetricsMonitor::Instance().RegisterHistogram(name, desc);
            }
        }
    }

    void reset() { m_histogram.resetFreqs(); }

    double percentile(float pcntl) {
        auto freqs = m_histogram.getFreqs();
        std::vector<int64_t> cum_freq( 0, m_histogram.getBucketCnt()+1 );
        int64_t fcount = 0;
        for (auto i = 0U; i < cum_freq.size(); i++) {
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
        for (auto i = 0U; i < m_histogram.getBucketCnt()+1; i++) {
            fcount += (m_histogram.getFreqs())[i];
        }
        return (fcount ? m_histogram.getSum()/fcount : 0);
    }

    void merge(const _histogram &other) { m_histogram.merge(other); }

    const std::string getName() { return m_name; }

    const std::string getDesc() { return m_desc; }

    const std::string getSubType() { return m_sub_type; }

    void publish() {
        //std::vector<double> vec(std::begin(m_histogram.getFreqs()), std::end(m_histogram.getFreqs()));
        //m_prometheus_hist->Update(vec, m_histogram.m_sum);
    }

private:
    const std::string m_name, m_desc, m_sub_type;
    _histogram m_histogram;
   //monitor::Histogram *m_prometheus_hist;
};

class ReportMetrics {
public:
    ReportMetrics() {
        urcu::urcu_ctl::register_rcu();
    }

    ~ReportMetrics() {
        urcu::urcu_ctl::unregister_rcu();
    }

    uint64_t registerCounter( std::string name, std::string desc, std::string sub_type, int64_t init_val ) {
        m_counters.emplace_back(name, desc, sub_type, 0);
        m_controller.fetchMetrics()->addCounter(init_val);
        return m_counters.size()-1;
    }
 
    uint64_t registerGauge( std::string name, std::string desc, std::string sub_type, int64_t init_val ) {

        m_gauges.emplace_back(name, desc, sub_type, 0);
        m_controller.fetchMetrics()->addGauge(init_val);
        return m_gauges.size()-1;
    }
 
    uint64_t registerHistogram( std::string name, std::string desc, std::string sub_type,
            std::vector<uint64_t> buckets =
                {   300,    450,    750,    1000,   3000,   5000,    7000,    9000,    11000,
                    13000,  15000,  17000,  19000,  21000,  32000,   45000,   75000,   110000,
                    160000, 240000, 360000, 540000, 800000, 1200000, 1800000, 2700000, 4000000  } ) {

        m_histograms.emplace_back(name, desc, sub_type, buckets);
        m_controller.fetchMetrics()->addHistogram(buckets);
        return m_histograms.size()-1;
    }

    _counter* getCounter(uint64_t index) {
        return m_controller.fetchMetrics()->fetchCounter(index);
    }

    _gauge* getGauge(uint64_t index) {
        return m_controller.fetchMetrics()->fetchGauge(index);
    }

    _histogram* getHistogram(uint64_t index) {
        return m_controller.fetchMetrics()->fetchHistogram(index);
    }

    /* This method gathers the metrics from all the threads, merge them to the base metrics and use base metrics
     * prometheus to update promethues metrics and send it */
    void gather() {
        auto metrics = m_controller.fetchMetrics();

        for (auto i = 0U; i < metrics->numCounters(); i++) {
            m_counters[i].reset();
            m_counters[i].merge(*metrics->fetchCounter(i));
        }
        for (auto i = 0U; i < metrics->numGauges(); i++) {
            m_gauges[i].merge(*metrics->fetchGauge(i));
        }
        for (auto i = 0U; i < metrics->numHistograms(); i++) {
            m_histograms[i].reset();
            m_histograms[i].merge(*metrics->fetchHistogram(i));
        }
        /* replace new metrics instance */
        //m_controller.swap();
        urcu::urcu_ctl::declare_quiscent_state();
    }

    void publish() {
        for (auto i : m_counters)     { i.publish(); }
        for (auto i : m_gauges)       { i.publish(); }
        for (auto i : m_histograms)   { i.publish(); }
    }

    std::string getJSON() {
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

private:
    std::vector<ReportCounter>      m_counters;
    std::vector<ReportGauge>        m_gauges;
    std::vector<ReportHistogram>    m_histograms;
    MetricsController               m_controller;
};

}
