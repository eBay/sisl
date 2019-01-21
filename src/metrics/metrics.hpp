/*
 * Created by Hari Kadayam, Sounak Gupta on Dec-12 2018
 *
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "histogram_buckets.hpp"
#include "wisr/wisr_framework.hpp"
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <utility>
#include <boost/preprocessor/facilities/expand.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <sds_logging/logging.h>
#include <sds_options/options.h>

// TODO: Commenting out this tempoarily till the SDS_OPTIONS and SDS_LOGGING issue is resolved
// SDS_LOGGING_DECL(vmod_metrics_framework)

namespace sisl {

class MetricsGroup;

enum _publish_as {
    publish_as_counter,
    publish_as_gauge,
    publish_as_histogram,
};

class CounterValue {
public:
    CounterValue() = default;
    void    increment(int64_t value = 1) { m_value += value; }
    void    decrement(int64_t value = 1) { m_value -= value; }
    int64_t get() const { return m_value; }
    int64_t merge(const CounterValue& other) {
        this->m_value += other.m_value;
        return this->m_value;
    }

private:
    int64_t m_value = 0;
};

class GaugeValue {
public:
    GaugeValue() : m_value(0) {}
    GaugeValue(const std::atomic< int64_t >& oval) : m_value(oval.load(std::memory_order_relaxed)) {}
    GaugeValue(const GaugeValue& other) : m_value(other.get()) {}
    GaugeValue& operator=(const GaugeValue& other) {
        m_value.store(other.get(), std::memory_order_relaxed);
        return *this;
    }
    void    update(int64_t value) { m_value.store(value, std::memory_order_relaxed); }
    int64_t get() const { return m_value.load(std::memory_order_relaxed); }

private:
    std::atomic< int64_t > m_value;
};

class HistogramValue {
public:
    void observe(int64_t value, const hist_bucket_boundaries_t& boundaries) {
        auto lower = std::lower_bound(boundaries.begin(), boundaries.end(), value);
        auto bkt_idx = lower - boundaries.begin();
        m_freqs[bkt_idx]++;
        m_sum += value;
    }

    void merge(const HistogramValue& other, const hist_bucket_boundaries_t& boundaries) {
        for (auto i = 0U; i < boundaries.size(); i++) {
            this->m_freqs[i] += other.m_freqs[i];
        }
        this->m_sum += other.m_sum;
    }
    auto&   get_freqs() const { return m_freqs; }
    int64_t get_sum() const { return m_sum; }

private:
    std::array< int64_t, HistogramBuckets::max_hist_bkts > m_freqs;
    int64_t                                                m_sum = 0;
};

static_assert(std::is_trivially_copyable< HistogramValue >::value, "Expecting HistogramValue to be trivally copyable");

class CounterInfo {
public:
    CounterInfo(const std::string& name, const std::string& desc, _publish_as ptype = publish_as_counter) :
            CounterInfo(name, desc, "", ptype) {}
    CounterInfo(const std::string& name, const std::string& desc, const std::string& sub_type,
                _publish_as ptype = publish_as_counter) :
            m_name(name),
            m_desc(desc),
            m_sub_type(sub_type) {
        if (name != "none") {
            if (sub_type != "") {
                if (ptype == publish_as_counter) {
                } else if (ptype == publish_as_gauge) {
                }
                // m_prometheus_counter =
                //    monitor::MetricsMonitor::Instance().RegisterCounter(name, desc,
                //    {{"type", sub_type}});
            } else {
                // m_prometheus_counter =
                // monitor::MetricsMonitor::Instance().RegisterCounter(name, desc);
            }
        }
    }

    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string sub_type() const { return m_sub_type; }
    void        publish(const CounterValue& value) {
        (void)value;
        // m_prometheus_counter->Update((double) value.get());
    }

private:
    std::string m_name;
    std::string m_desc;
    std::string m_sub_type;
    // monitor::Counter* m_prometheus_counter = nullptr;
    // monitor::Gauge* m_prometheus_gauge; // In case counter to be represented as gauge
};

class GaugeInfo {
    friend class MetricsGroup;

public:
    GaugeInfo(const std::string& name, const std::string& desc) : GaugeInfo(name, desc, "") {}
    GaugeInfo(const std::string& name, const std::string& desc, const std::string& sub_type) :
            m_name(name),
            m_desc(desc),
            m_sub_type(sub_type) {

        if (name != "none") {
            if (sub_type != "") {
                // m_prometheus_gauge =
                //    monitor::MetricsMonitor::Instance().RegisterGauge(name, desc,
                //    {{"type", sub_type}});
            } else {
                // m_prometheus_gauge =
                // monitor::MetricsMonitor::Instance().RegisterGauge(name, desc);
            }
        }
    }

    uint64_t    get() const { return m_gauge.get(); };
    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string sub_type() const { return m_sub_type; }
    void        publish() {
        // m_prometheus_gauge->Set((double) m_gauge.get());
    }

private:
    const std::string m_name, m_desc, m_sub_type;
    GaugeValue        m_gauge;
    // monitor::Gauge *m_prometheus_gauge;
};

class HistogramInfo {
public:
    HistogramInfo(const std::string& name, const std::string& desc, const std::string& sub_type,
                  const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets)) :
            m_name(name),
            m_desc(desc),
            m_sub_type(sub_type),
            m_bkt_boundaries(bkt_boundaries) {

        if (name != "none") {
            if (sub_type != "") {
                // m_prometheus_hist =
                //    monitor::MetricsMonitor::Instance().RegisterHistogram(name, desc,
                //    {{"type", sub_type}});
            } else {
                // m_prometheus_hist =
                // monitor::MetricsMonitor::Instance().RegisterHistogram(name, desc);
            }
        }
    }

    double percentile(const HistogramValue& hvalue, float pcntl) const {
        std::array< int64_t, HistogramBuckets::max_hist_bkts > cum_freq;
        int64_t                                                fcount = 0;
        for (auto i = 0U; i < HistogramBuckets::max_hist_bkts; i++) {
            fcount += (hvalue.get_freqs())[i];
            cum_freq[i] = fcount;
        }

        int64_t pnum = fcount * pcntl / 100;
        auto    i = (std::lower_bound(cum_freq.begin(), cum_freq.end(), pnum)) - cum_freq.begin();
        if ((hvalue.get_freqs())[i] == 0)
            return 0;
        auto   Yl = i == 0 ? 0 : m_bkt_boundaries[i - 1];
        auto   ith_cum_freq = (i == 0) ? 0 : cum_freq[i - 1];
        double Yp = Yl + (((pnum - ith_cum_freq) * i) / (hvalue.get_freqs())[i]);
        return Yp;

        /* Formula:
            Yp = lower bound of i-th bucket + ((pn - cumfreq[i-1]) * i ) / freq[i]
            where
                pn = (cnt * percentile)/100
                i  = matched index of pnum in cum_freq
         */
    }

    int64_t count(const HistogramValue& hvalue) const {
        int64_t cnt = 0;
        for (auto i = 0U; i < HistogramBuckets::max_hist_bkts; i++) {
            cnt += (hvalue.get_freqs())[i];
        }
        return cnt;
    }

    double average(const HistogramValue& hvalue) const {
        auto cnt = count(hvalue);
        return (cnt ? hvalue.get_sum() / cnt : 0);
    }

    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string sub_type() const { return m_sub_type; }
    void        publish(const HistogramValue& hvalue) {
        (void)hvalue;
        // std::vector<double> vec(std::begin(hvalue.getFreqs()),
        // std::end(hvalue.get_freqs())); m_prometheus_hist->Update(vec,
        // hvalue.m_sum);
    }

    const hist_bucket_boundaries_t& get_boundaries() const { return m_bkt_boundaries; }

private:
    const std::string               m_name, m_desc, m_sub_type;
    const hist_bucket_boundaries_t& m_bkt_boundaries;
    // monitor::Histogram *m_prometheus_hist;
};

class SafeMetrics {
private:
    CounterValue*                       m_counters = nullptr;
    HistogramValue*                     m_histograms = nullptr;
    const std::vector< HistogramInfo >& m_histogram_info;

    uint32_t m_ncntrs;
    uint32_t m_nhists;

public:
    SafeMetrics(const std::vector< HistogramInfo >& hinfo, uint32_t ncntrs, uint32_t nhists) :
            m_histogram_info(hinfo),
            m_ncntrs(ncntrs),
            m_nhists(nhists) {
        m_counters = new CounterValue[ncntrs];
        m_histograms = new HistogramValue[nhists];

        memset((void *)m_counters, 0, (sizeof(CounterValue) * ncntrs));
        memset((void *)m_histograms, 0, (sizeof(HistogramValue) * nhists));

#if 0
        LOG("ThreadId=%08lux: SafeMetrics=%p constructor, m_counters=%p, m_histograms=%p\n",
                pthread_self(), (void *)this, (void *)m_counters, (void *)m_histograms);
#endif
    }

    ~SafeMetrics() {
        delete[] m_counters;
        delete[] m_histograms;
        // LOGTRACEMOD(vmod_metrics_framework, "ThreadId={}, SafeMetrics={} destructor\n", pthread_self(), (void
        // *)this);
    }

    // Required method to work with wisr_framework
    static void merge(SafeMetrics* a, SafeMetrics* b) {
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

    CounterValue& get_counter(uint64_t index) {
        assert(index < m_ncntrs);
        return m_counters[index];
    }

    HistogramValue& get_histogram(uint64_t index) {
        assert(index < m_nhists);
        return m_histograms[index];
    }

    auto get_num_metrics() const { return std::make_tuple(m_ncntrs, m_nhists); }
};

typedef std::shared_ptr< MetricsGroup >                                                              MetricsGroupPtr;
typedef sisl::wisr_framework< SafeMetrics, const std::vector< HistogramInfo >&, uint32_t, uint32_t > ThreadSafeMetrics;

class MetricsGroupResult;
class MetricsFarm;
class MetricsGroup {
    friend class MetricsFarm;
    friend class MetricsResult;

private:
    [[nodiscard]] auto lock() { return std::lock_guard< decltype(m_mutex) >(m_mutex); }

public:
    static MetricsGroupPtr make_group() { return std::make_shared< MetricsGroup >(); }

    MetricsGroup(const char* name = nullptr) {
        if (name) {
            m_grp_name = name;
        } else {
            std::stringstream ss;
            ss << "metrics_group_" << __COUNTER__;
            m_grp_name = ss.str();
        }
    }

    MetricsGroup(const std::string& name) {
        if (!name.empty()) {
            m_grp_name = name;
        } else {
            std::stringstream ss;
            ss << "metrics_group_" << __COUNTER__;
            m_grp_name = ss.str();
        }
    }

    uint64_t register_counter(const std::string& name, const std::string& desc, const std::string& sub_type = "",
                              _publish_as ptype = publish_as_counter) {
        auto locked = lock();
        m_counters.emplace_back(name, desc, sub_type, ptype);
        return m_counters.size() - 1;
    }
    uint64_t register_counter(const CounterInfo& counter) {
        auto locked = lock();
        m_counters.push_back(counter);
        return m_counters.size() - 1;
    }

    uint64_t register_gauge(const std::string& name, const std::string& desc, const std::string& sub_type = "") {
        auto locked = lock();
        m_gauges.emplace_back(name, desc, sub_type);
        return m_gauges.size() - 1;
    }
    uint64_t register_gauge(const GaugeInfo& gauge) {
        auto locked = lock();
        m_gauges.push_back(gauge);
        return m_gauges.size() - 1;
    }

    uint64_t register_histogram(const std::string& name, const std::string& desc, const std::string& sub_type = "",
                                const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets)) {
        auto locked = lock();
        m_histograms.emplace_back(name, desc, sub_type, bkt_boundaries);
        m_bkt_boundaries.push_back(bkt_boundaries);
        return m_histograms.size() - 1;
    }
    uint64_t register_histogram(HistogramInfo& hist) {
        auto locked = lock();
        m_histograms.push_back(hist);
        m_bkt_boundaries.push_back(hist.get_boundaries());
        return m_histograms.size() - 1;
    }
    uint64_t register_histogram(const std::string& name, const std::string& desc,
                                const hist_bucket_boundaries_t& bkt_boundaries) {
        return register_histogram(name, desc, "", bkt_boundaries);
    }

    void counter_increment(uint64_t index, int64_t val = 1) {
        m_metrics->insertable()->get_counter(index).increment(val);
#if 0
        printf("Thread=%08lux: Updating SafeMetrics=%p countervalue = %p, index=%lu to new val = %ld\n", pthread_self(),
               m_metrics->insertable(), &m_metrics->insertable()->getCounter(index), index, m_metrics->insertable()->get_counter(index).get());
#endif
    }
    void counter_decrement(uint64_t index, int64_t val = 1) {
        m_metrics->insertable()->get_counter(index).decrement(val);
    }
    void gauge_update(uint64_t index, int64_t val) { m_gauges[index].m_gauge.update(val); }
    void histogram_observe(uint64_t index, int64_t val) {
        m_metrics->insertable()->get_histogram(index).observe(val, m_bkt_boundaries[index]);
#if 0
        printf("Thread=%08lux: Updating SafeMetrics=%p histvalue = %p, index=%lu\n",
               pthread_self(), m_metrics->insertable(), &m_metrics->insertable()->get_histogram(index), index);
#endif
    }

    const CounterInfo&   get_counter_info(uint64_t index) const { return m_counters[index]; }
    const GaugeInfo&     get_gauge_info(uint64_t index) const { return m_gauges[index]; }
    const HistogramInfo& get_histogram_info(uint64_t index) const { return m_histograms[index]; }

    size_t total_registered_counters() const { return m_counters.size(); }
    size_t total_registered_gauges() const { return m_gauges.size(); }
    size_t total_registered_histograms() const { return m_histograms.size(); }

    nlohmann::json get_result_in_json(bool need_latest = true) {
        nlohmann::json json;
        nlohmann::json counter_entries;
        nlohmann::json gauge_entries;
        nlohmann::json hist_entries;

        gather_result(need_latest,
                      [&counter_entries](CounterInfo& c, const CounterValue& result) {
                          std::string desc = c.desc();
                          if (!c.sub_type().empty())
                              desc = desc + " - " + c.sub_type();
                          counter_entries[desc] = result.get();
                      },
                      [&gauge_entries](GaugeInfo& g) {
                          std::string desc = g.desc();
                          if (!g.sub_type().empty())
                              desc = desc + " - " + g.sub_type();
                          gauge_entries[desc] = g.get();
                      },
                      [&hist_entries](HistogramInfo& h, const HistogramValue& result) {
                          std::stringstream ss;
                          ss << h.average(result) << " / " << h.percentile(result, 50) << " / "
                             << h.percentile(result, 95) << " / " << h.percentile(result, 99);
                          std::string desc = h.desc();
                          if (!h.sub_type().empty())
                              desc = desc + " - " + h.sub_type();
                          hist_entries[desc] = ss.str();
                      });

        json["Counters"] = counter_entries;
        json["Gauges"] = gauge_entries;
        json["Histograms percentiles (usecs) avg/50/95/99"] = hist_entries;
        return json;
    }

    void publish_result() {
        gather_result(true, /* need_latest */
                      [](CounterInfo& c, const CounterValue& result) { c.publish(result); },
                      [](GaugeInfo& g) { g.publish(); },
                      [](HistogramInfo& h, const HistogramValue& result) { h.publish(result); });
    }

    const std::string& get_name() const { return m_grp_name; }

private:
    void on_register() {
        m_metrics = std::make_unique< ThreadSafeMetrics >(m_histograms, m_counters.size(), m_histograms.size());
    }

    void gather_result(bool need_latest, std::function< void(CounterInfo&, const CounterValue&) > counter_cb,
                       std::function< void(GaugeInfo&) >                            gauge_cb,
                       std::function< void(HistogramInfo&, const HistogramValue&) > histogram_cb) {
        auto smetrics = need_latest ? m_metrics->now() : m_metrics->delayed();

        for (auto i = 0U; i < m_counters.size(); i++) {
            counter_cb(m_counters[i], smetrics->get_counter(i));
        }

        for (auto i = 0U; i < m_gauges.size(); i++) {
            gauge_cb(m_gauges[i]);
        }

        for (auto i = 0U; i < m_histograms.size(); i++) {
            histogram_cb(m_histograms[i], smetrics->get_histogram(i));
        }
    }

private:
    std::string                          m_grp_name;
    std::mutex                           m_mutex;
    std::unique_ptr< ThreadSafeMetrics > m_metrics;

    std::vector< CounterInfo >                                              m_counters;
    std::vector< GaugeInfo >                                                m_gauges;
    std::vector< HistogramInfo >                                            m_histograms;
    std::vector< std::reference_wrapper< const hist_bucket_boundaries_t > > m_bkt_boundaries;
};

class MetricsFarm {
private:
    std::set< MetricsGroupPtr > m_mgroups;
    std::mutex                  m_lock;

private:
    MetricsFarm() = default;

    [[nodiscard]] auto lock() { return std::lock_guard< decltype(m_lock) >(m_lock); }

public:
    friend class MetricsResult;

    static MetricsFarm& getInstance() {
        static MetricsFarm instance;
        return instance;
    }

    MetricsFarm(const MetricsFarm&) = delete;
    void operator=(const MetricsFarm&) = delete;

    void register_metrics_group(MetricsGroupPtr mgroup) {
        assert(mgroup != nullptr);
        auto locked = lock();
        mgroup->on_register();
        m_mgroups.insert(mgroup);
    }

    void deregister_metrics_group(MetricsGroupPtr mgroup) {
        assert(mgroup != nullptr);
        auto locked = lock();
        m_mgroups.erase(mgroup);
    }

    nlohmann::json get_result_in_json(bool need_latest = true) {
        nlohmann::json json;
        for (auto& mgroup : m_mgroups) {
            json[mgroup->get_name()] = mgroup->get_result_in_json(need_latest);
        }
        return json;
    };
    std::string get_result_in_json_string(bool need_latest = true) { return get_result_in_json(need_latest).dump(); }

    void publish_result() {
        for (auto& mgroup : m_mgroups) {
            mgroup->publish_result();
        }
    }
};

} // namespace sisl

////////////////////////////////////////// Helper Routine section ////////////////////////////////////////////////
template < char... chars >
using tstring = std::integer_sequence< char, chars... >;

template < typename T, T... chars >
constexpr tstring< chars... > operator""_tstr() {
    return {};
}

namespace sisl {

template < typename >
struct NamedCounter;

template < typename >
struct NamedGauge;

template < typename >
struct NamedHistogram;

template < char... elements >
struct NamedCounter< tstring< elements... > > {
public:
    static constexpr char Name[sizeof...(elements) + 1] = {elements..., '\0'};
    int                   m_index = -1;

    static NamedCounter& getInstance() {
        static NamedCounter instance;
        return instance;
    }

    CounterInfo create(const std::string& desc, const std::string& sub_type = "",
                       _publish_as ptype = publish_as_counter) {
        return CounterInfo(std::string(Name), desc, sub_type, ptype);
    }

    const char* get_name() const { return Name; }
};

template < char... elements >
struct NamedGauge< tstring< elements... > > {
public:
    static constexpr char Name[sizeof...(elements) + 1] = {elements..., '\0'};
    int                   m_index = -1;

    static NamedGauge& getInstance() {
        static NamedGauge instance;
        return instance;
    }

    NamedGauge() { std::cout << "Creating named gauge for " << Name << " ptr = " << (void*)this << "\n"; }
    GaugeInfo create(const std::string& desc, const std::string& sub_type = "") {
        return GaugeInfo(std::string(Name), desc, sub_type);
    }

    const char* get_name() const { return Name; }
};

template < char... elements >
struct NamedHistogram< tstring< elements... > > {
public:
    static constexpr char Name[sizeof...(elements) + 1] = {elements..., '\0'};
    int                   m_index = -1;

    static NamedHistogram& getInstance() {
        static NamedHistogram instance;
        return instance;
    }

    HistogramInfo create(const std::string& desc, const std::string& sub_type = "",
                         const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets)) {
        return HistogramInfo(std::string(Name), desc, sub_type, bkt_boundaries);
    }

    HistogramInfo create(const std::string& desc, const hist_bucket_boundaries_t& bkt_boundaries) {
        return HistogramInfo(std::string(Name), desc, "", bkt_boundaries);
    }

    const char* get_name() const { return Name; }
};

class MetricsGroupWrapper : public MetricsGroupPtr {
public:
    explicit MetricsGroupWrapper(const char* grp_name) : MetricsGroupPtr(std::make_shared< MetricsGroup >(grp_name)) {}
    explicit MetricsGroupWrapper(const std::string& grp_name) :
            MetricsGroupPtr(std::make_shared< MetricsGroup >(grp_name)) {}
    void register_me_to_farm() { MetricsFarm::getInstance().register_metrics_group(*this); }
};

#define REGISTER_COUNTER(name, ...)                                                                                    \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        auto& nc = sisl::NamedCounter< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance();       \
        auto  rc = nc.create(__VA_ARGS__);                                                                             \
        nc.m_index = this->get()->register_counter(rc);                                                                \
    }

#define REGISTER_GAUGE(name, ...)                                                                                      \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        auto& ng = sisl::NamedGauge< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance();         \
        auto  rg = ng.create(__VA_ARGS__);                                                                             \
        ng.m_index = this->get()->register_gauge(rg);                                                                  \
    }

#define REGISTER_HISTOGRAM(name, ...)                                                                                  \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        auto& nh = sisl::NamedHistogram< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance();     \
        auto  rh = nh.create(__VA_ARGS__);                                                                             \
        nh.m_index = this->get()->register_histogram(rh);                                                              \
    }

#define METRIC_NAME_TO_INDEX(type, name)                                                                               \
    (sisl::type< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance().m_index)

// TODO: Replace printf and #ifnef DEBUG with DLOGDFATAL_IF once the issue of SDS_LOGGING and SDS_OPTIONS are resolved
#ifndef NDEBUG
#define __VALIDATE_AND_EXECUTE(group, type, method, name, ...)                                                         \
    {                                                                                                                  \
        auto i = METRIC_NAME_TO_INDEX(type, name);                                                                     \
        if (i == -1) {                                                                                                 \
            fprintf(stderr, "Metric name '%s' not registered yet but used\n", BOOST_PP_STRINGIZE(name));               \
            fflush(stderr);                                                                                            \
            assert(0);                                                                                                 \
        }                                                                                                              \
        ((group)->method(i, __VA_ARGS__));                                                                             \
    }
#else
#define __VALIDATE_AND_EXECUTE(group, type, method, name, ...)                                                         \
    {                                                                                                                  \
        auto i = METRIC_NAME_TO_INDEX(type, name);                                                                     \
        ((group)->method(i, __VA_ARGS__));                                                                             \
    }
#endif

#define __VALIDATE_AND_EXECUTE_IF_ELSE(group, type, method, cond, namea, nameb, ...)                                   \
    if (cond) {                                                                                                        \
        __VALIDATE_AND_EXECUTE(group, type, method, namea, __VA_ARGS__);                                               \
    } else {                                                                                                           \
        __VALIDATE_AND_EXECUTE(group, type, method, nameb, __VA_ARGS__);                                               \
    }

#define COUNTER_INCREMENT(group, name, ...)                                                                            \
    __VALIDATE_AND_EXECUTE(group, NamedCounter, counter_increment, name, __VA_ARGS__)
#define COUNTER_INCREMENT_IF_ELSE(group, cond, namea, nameb, ...)                                                      \
    __VALIDATE_AND_EXECUTE_IF_ELSE(group, NamedCounter, counter_increment, cond, namea, nameb, __VA_ARGS__)
#define COUNTER_DECREMENT(group, name, ...)                                                                            \
    __VALIDATE_AND_EXECUTE(group, NamedCounter, counter_decrement, name, __VA_ARGS__)
#define COUNTER_DECREMENT_IF_ELSE(group, cond, namea, nameb, ...)                                                      \
    __VALIDATE_AND_EXECUTE_IF_ELSE(group, NamedCounter, counter_decrement, cond, namea, nameb, __VA_ARGS__)

#define GAUGE_UPDATE(group, name, ...) __VALIDATE_AND_EXECUTE(group, NamedGauge, gauge_update, name, __VA_ARGS__)
#define GAUGE_UPDATE_IF_ELSE(group, name, ...)                                                                         \
    __VALIDATE_AND_EXECUTE_IF_ELSE(group, NamedGauge, gauge_update, name, __VA_ARGS__)

#define HISTOGRAM_OBSERVE(group, name, ...)                                                                            \
    __VALIDATE_AND_EXECUTE(group, NamedHistogram, histogram_observe, name, __VA_ARGS__)
#define HISTOGRAM_OBSERVE_IF_ELSE(group, name, ...)                                                                    \
    __VALIDATE_AND_EXECUTE_IF_ELSE(group, NamedHistogram, histogram_observe, name, __VA_ARGS__)

//#define GAUGE_UPDATE(group, name, ...) ((group)->gauge_update(METRIC_NAME_TO_INDEX(name), __VA_ARGS__))
//#define HISTOGRAM_OBSERVE(group, name, ...) ((group)->histogram_observe(METRIC_NAME_TO_INDEX(name), __VA_ARGS__))

#if 0
#define COUNTER_INCREMENT(group, name, ...)                                                                            \
    {                                                                                                                  \
        auto& nc = NamedCounter< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance();             \
        std::cout << "Counter accessed for name = " << nc.get_name() << " ptr = " << (void*)&nc                        \
                  << " index = " << nc.m_index << "\n";                                                                \
        group->counterIncrement(nc.m_index, __VA_ARGS__);                                                              \
    }
#endif

} // namespace sisl
