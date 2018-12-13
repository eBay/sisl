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
#include <mutex>
#include <thread>
#include <set>
#include <iostream>
#include <sstream>
#include <map>

#include "thread_buffer.hpp"
#include <nlohmann/json.hpp>
#include "histogram_buckets.hpp"
#include <utility>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

namespace sisl { namespace metrics {

#if 0
std::once_flag is_bktset_present;
class HistogramBucketsSet {
public:
    static std::unique_ptr< HistogramBucketsSet > m_instance;

    static HistogramBucketsSet *getInstance() {
        std::call_once(is_bktset_present, [](){
            m_instance = std::make_unique< HistogramBucketsSet >();
        });
        return m_instance.get();
    }

    static void registerBkts(const char *name, const hist_bucket_boundaries_t& bucket_boundaries) {
        getInstance()->_register(name, bucket_boundaries);
    }

    static hist_bucket_boundaries_t& find(const char *name) {
        return getInstance()->_find(name);
    }

private:
    HistogramBucketsSet() {
        m_map = {
            { DEFAULT_HIST_LATENCY_BUCKETS,
                 {
                     300, 450, 750, 1000, 3000, 5000, 7000, 9000, 11000, 13000, 15000, 17000, 19000, 21000, 32000,
                     45000, 75000, 110000, 160000, 240000, 360000, 540000, 800000, 1200000, 1800000, 2700000, 4000000
                 }
            },

            {"ExponentialOfTwoBuckets",
                {
                    1,        exp2(1),  exp2(2),  exp2(3),  exp2(4),  exp2(5),  exp2(6),  exp2(7),
                    exp2(8),  exp2(9),  exp2(10), exp2(11), exp2(12), exp2(13), exp2(14), exp2(15),
                    exp2(16), exp2(17), exp2(18), exp2(19), exp2(20), exp2(21), exp2(22), exp2(23),
                    exp2(24), exp2(25), exp2(26), exp2(27), exp2(28), exp2(29), exp2(30), exp2(31))
                }
            }
        };
    }

private:
    std::mutex m_lock;
    std::map< std::string, hist_bucket_boundaries_t > m_map;

    [[nodiscard]] auto lock() { return std::lock_guard<decltype(m_lock)>(m_lock); }

    void _register(const char *name, const hist_bucket_boundaries_t& bucket_boundaries) {
        auto locked = lock();
        m_map.emplace(name, bucket_boundaries);
    }

    hist_bucket_boundaries_t& _find(const char *name) {
        auto locked = lock();
        auto it = m_map.find(name);
        if (it == m_map.end()) { it = m_map.begin(); }
        return it->second;
    }
};
#endif

class MetricsGroup;

enum _publish_as { publish_as_counter, publish_as_gauge, publish_as_histogram, };

class _counter {
public:
    _counter() = default;
    void increment (int64_t value = 1)  { m_value += value; }
    void decrement (int64_t value = 1)  { m_value -= value; }
    int64_t get() const { return m_value; }
    int64_t merge (const _counter& other) {
        this->m_value += other.m_value;
        return this->m_value;
    }
private:
    int64_t m_value = 0;
};

class _gauge {
public:
    _gauge() : m_value(0) {}
    _gauge(const std::atomic<int64_t> &oval) : m_value(oval.load(std::memory_order_relaxed)) {}
    _gauge(const _gauge &other) : m_value(other.get()) {}
    _gauge &operator=(const _gauge &other) {
        m_value.store(other.get(), std::memory_order_relaxed);
        return *this;
    }
    void update (int64_t value) { m_value.store(value, std::memory_order_relaxed); }
    int64_t get() const { return m_value.load(std::memory_order_relaxed); }

private:
    std::atomic<int64_t> m_value;
};

class _histogram {
public:
    void observe(int64_t value, const hist_bucket_boundaries_t &boundaries) {
        auto lower = std::lower_bound(boundaries.begin(), boundaries.end(), value);
        auto bkt_idx = lower - boundaries.begin();
        m_freqs[bkt_idx]++;
        m_sum += value;
    }

    void merge(const _histogram& other, const hist_bucket_boundaries_t& boundaries) {
        for (auto i = 0U; i < boundaries.size(); i++) {
            this->m_freqs[i] += other.m_freqs[i];
        }
        this->m_sum += other.m_sum;
    }
    auto& getFreqs() const { return m_freqs; }
    int64_t getSum() const { return m_sum; }

private:
    std::array< int64_t, HistogramBuckets::max_hist_bkts > m_freqs;
    int64_t m_sum = 0;
};

static_assert(std::is_trivially_copyable<_histogram>::value, "Expecting _histogram to be trivally copyable");

class ReportCounter {
public:
    ReportCounter(const std::string& name, const std::string& desc, _publish_as ptype = publish_as_counter) :
        ReportCounter(name, desc, "", ptype) {}
    ReportCounter(const std::string& name, const std::string& desc, const std::string& sub_type,
            _publish_as ptype = publish_as_counter) :
            m_name(name),
            m_desc(desc),
            m_sub_type(sub_type) {
        if (name != "none") {
            if (sub_type != "") {
                if (ptype == publish_as_counter) {
                } else if (ptype == publish_as_gauge) {
                }
                //m_prometheus_counter =
                //    monitor::MetricsMonitor::Instance().RegisterCounter(name, desc, {{"type", sub_type}});
            } else {
                //m_prometheus_counter = monitor::MetricsMonitor::Instance().RegisterCounter(name, desc);
            }
        }
    }

    int64_t get() const { return m_counter.get(); }
    int64_t merge(const _counter& other) { return m_counter.merge(other); }
    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string subType() const { return m_sub_type; }
    void publish() {
        //m_prometheus_counter->Update((double) m_counter.get());
    }
private:
    std::string m_name;
    std::string m_desc;
    std::string m_sub_type;
    _counter m_counter;
    //monitor::Counter* m_prometheus_counter = nullptr;
    //monitor::Gauge* m_prometheus_gauge; // In case counter to be represented as gauge
};

class ReportGauge {
    friend class MetricsGroup;
public:
    ReportGauge(const std::string& name, const std::string& desc) : ReportGauge(name, desc, "") {}
    ReportGauge(const std::string& name, const std::string& desc, const std::string& sub_type) :
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

    uint64_t get() const { return m_gauge.get(); };
    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string subType() const { return m_sub_type; }
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
    ReportHistogram(const std::string& name, const std::string& desc, const std::string& sub_type,
            const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets)) :
            m_name(name),
            m_desc(desc),
            m_sub_type(sub_type),
            m_bkt_boundaries(bkt_boundaries) {
 
        if (name != "none") {
            if (sub_type != "") {
                //m_prometheus_hist =
                //    monitor::MetricsMonitor::Instance().RegisterHistogram(name, desc, {{"type", sub_type}});
            } else {
                //m_prometheus_hist = monitor::MetricsMonitor::Instance().RegisterHistogram(name, desc);
            }
        }
    }
    double percentile( float pcntl ) const {
        std::array<int64_t, HistogramBuckets::max_hist_bkts> cum_freq;
        int64_t fcount = 0;
        for (auto i = 0U; i < HistogramBuckets::max_hist_bkts; i++) {
            fcount += (m_histogram.getFreqs())[i];
            cum_freq[i] = fcount;
        }

        int64_t pnum = fcount * pcntl/100;
        auto i = (std::lower_bound(cum_freq.begin(), cum_freq.end(), pnum)) - cum_freq.begin();
        if ( (m_histogram.getFreqs())[i] == 0 ) return 0;
        auto Yl = i == 0 ? 0 : m_bkt_boundaries[i-1];
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
    int64_t count() const {
        int64_t cnt = 0;
        for (auto i = 0U; i < HistogramBuckets::max_hist_bkts; i++) {
            cnt += (m_histogram.getFreqs())[i];
        }
        return cnt;
    }
    double average() const {
        auto cnt = count();
        return (cnt ? m_histogram.getSum()/cnt : 0);
    }
    void merge(const _histogram& other) { m_histogram.merge(other, m_bkt_boundaries); }
    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string subType() const { return m_sub_type; }
    void publish() {
        //std::vector<double> vec(std::begin(m_histogram.getFreqs()), std::end(m_histogram.getFreqs()));
        //m_prometheus_hist->Update(vec, m_histogram.m_sum);
    }
    _histogram& getReportHistogram() { return m_histogram; }

    const hist_bucket_boundaries_t& getBoundaries() const { return m_bkt_boundaries; }
private:
    const std::string m_name, m_desc, m_sub_type;
    const hist_bucket_boundaries_t& m_bkt_boundaries;
    _histogram m_histogram;
   //monitor::Histogram *m_prometheus_hist;
};

class SafeMetrics {
private:
    _counter   *m_counters   = nullptr;
    _histogram *m_histograms = nullptr;

    uint32_t m_ncntrs;
    uint32_t m_nhists;

public:
    SafeMetrics() : SafeMetrics(0U, 0U) {}

    SafeMetrics(uint32_t ncntrs, uint32_t nhists) :
            m_ncntrs(ncntrs),
            m_nhists(nhists) {
        m_counters   = new _counter[ncntrs];
        m_histograms = new _histogram[nhists];

        memset(m_counters, 0, (sizeof(_counter) * ncntrs));
        memset(m_histograms, 0, (sizeof(_histogram) * nhists));
    }

    ~SafeMetrics() {
        delete [] m_counters;
        delete [] m_histograms;
    }

    _counter& getCounter (uint64_t index) { return m_counters[index]; }
    _histogram& getHistogram (uint64_t index) { return m_histograms[index]; }

    auto getNumMetrics() const { return std::make_tuple(m_ncntrs, m_nhists); }
};

class _metrics_buf {
public:
    _metrics_buf(uint32_t ncntrs, uint32_t nhists) :
            m_safe_metrics(ncntrs, nhists) {}

    urcu::urcu_ptr<SafeMetrics> getSafe() { return m_safe_metrics.get(); }
    void rotate() {
        uint32_t ncntrs, nhists;
        std::tie(ncntrs, nhists) = m_safe_metrics.get_node()->get()->getNumMetrics();
        m_safe_metrics.make_and_exchange(ncntrs, nhists);
    }

private:
    urcu::urcu_data<SafeMetrics> m_safe_metrics;
};

typedef std::shared_ptr<MetricsGroup> MetricsGroupPtr;
typedef fds::ThreadBuffer< _metrics_buf, uint32_t, uint32_t > MetricsThreadBuffer;

class MetricsGroupResult;
class MetricsFarm;
class MetricsGroup {
    friend class MetricsFarm;
    friend class MetricsResult;

public:
private:
    [[nodiscard]] auto lock() { return std::lock_guard<decltype(m_mutex)>(m_mutex); }

public:
    static MetricsGroupPtr make_group() { return std::make_shared<MetricsGroup>(); }

    MetricsGroup(const char *name = nullptr) {
        if (name) {
            m_grp_name = name;
        } else {
            std::stringstream ss; ss << "metrics_group_" << __COUNTER__;
            m_grp_name = ss.str();
        }
    }

    uint64_t registerCounter(const std::string& name, const std::string& desc, const std::string& sub_type = "",
            _publish_as ptype = publish_as_counter) {
        auto locked = lock();
        m_counters.emplace_back(name, desc, sub_type, ptype);
        return m_counters.size()-1;
    }
    uint64_t registerCounter(const ReportCounter& counter) {
        auto locked = lock();
        m_counters.push_back(counter);
        return m_counters.size()-1;
    }

    uint64_t registerGauge(const std::string& name, const std::string& desc, const std::string& sub_type = "") {
        auto locked = lock();
        m_gauges.emplace_back(name, desc, sub_type);
        return m_gauges.size()-1;
    }
    uint64_t registerGauge(const ReportGauge& gauge) {
        auto locked = lock();
        m_gauges.push_back(gauge);
        return m_gauges.size()-1;
    }

    uint64_t registerHistogram(const std::string& name, const std::string& desc, const std::string& sub_type = "",
            const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets)) {
        auto locked = lock();
        m_histograms.emplace_back(name, desc, sub_type, bkt_boundaries);
        m_bkt_boundaries.push_back(bkt_boundaries);
        return m_histograms.size()-1;
    }
    uint64_t registerHistogram(ReportHistogram &hist) {
        auto locked = lock();
        m_histograms.push_back(hist);
        m_bkt_boundaries.push_back(hist.getBoundaries());
        return m_histograms.size()-1;
    }
    uint64_t registerHistogram(const std::string& name, const std::string& desc,
            const hist_bucket_boundaries_t& bkt_boundaries) {
        return registerHistogram(name, desc, "", bkt_boundaries);
    }

    void counterIncrement(uint64_t index, int64_t val=1) { (*m_buffer)->getSafe()->getCounter(index).increment(val); }
    void counterDecrement(uint64_t index, int64_t val=1) { (*m_buffer)->getSafe()->getCounter(index).decrement(val); }
    void gaugeUpdate(uint64_t index, int64_t val) { m_gauges[index].m_gauge.update(val); }
    void histogramObserve(uint64_t index, int64_t val) {
        (*m_buffer)->getSafe()->getHistogram(index).observe(val, m_bkt_boundaries[index]);
    }

    const std::string& getName() const { return m_grp_name; }

#if 0
    _counter& getCounter(uint64_t index) { return (*m_buffer)->getSafe()->getCounter(index); }
    _gauge& getGauge(uint64_t index) { return m_gauges[index].m_gauge; }
    _histogram& getHistogram(uint64_t index) { return (*m_buffer)->getSafe()->getHistogram(index); }
#endif

    std::vector<ReportCounter>      m_counters;
    std::vector<ReportGauge>        m_gauges;
    std::vector<ReportHistogram>    m_histograms;
    std::vector<std::reference_wrapper< const hist_bucket_boundaries_t >> m_bkt_boundaries;

private:
    void on_register() {
        m_buffer = std::make_unique< MetricsThreadBuffer >(m_counters.size(), m_histograms.size());
    }
    std::unique_ptr<MetricsGroupResult> getResult() { return std::make_unique<MetricsGroupResult>(this, *m_buffer); }

private:
    std::string m_grp_name;
    std::mutex m_mutex;
    std::unique_ptr< MetricsThreadBuffer > m_buffer;
};

class MetricsGroupResult {
public:
    friend class MetricsResult;

    MetricsGroupResult(MetricsGroup* mgroup, MetricsThreadBuffer& all_buf) {
        m_mgroup = mgroup;
        all_buf.access_all_threads([mgroup](_metrics_buf *m) {
            /* get current metrics instance */
            auto metrics = m->getSafe();
            uint32_t num_cntrs, num_hists;
            std::tie(num_cntrs, num_hists) = metrics->getNumMetrics();

            for (auto i = 0U; i < num_cntrs; i++) {
                mgroup->m_counters[i].merge(metrics->getCounter(i));
            }
            for (auto i = 0U; i < num_hists; i++) {
                mgroup->m_histograms[i].merge(metrics->getHistogram(i));
            }
            /* replace new metrics instance */
            m->rotate();
        });
    }

    ~MetricsGroupResult() { urcu::urcu_ctl::declare_quiscent_state(); }

    void publish() {
        for (auto i = 0U; i < m_mgroup->m_counters.size(); i++) {
            m_mgroup->m_counters[i].publish();
        }
        for (auto i = 0U; i < m_mgroup->m_gauges.size(); i++) {
            m_mgroup->m_gauges[i].publish();
        }
        for (auto i = 0U; i < m_mgroup->m_histograms.size(); i++) {
            m_mgroup->m_histograms[i].publish();
        }
    }

    nlohmann::json getJSON() const {
        nlohmann::json json;
        nlohmann::json counter_entries;
        for (auto &c : m_mgroup->m_counters) {
            std::string desc = c.desc();
            if (!c.subType().empty()) desc = desc + " - " + c.subType();
            counter_entries[desc] = c.get();
        }
        json["Counters"] = counter_entries;

        nlohmann::json gauge_entries;
        for (auto &g : m_mgroup->m_gauges) {
            std::string desc = g.desc();
            if (!g.subType().empty()) desc = desc + " - " + g.subType();
            gauge_entries[desc] = g.get();
        }
        json["Gauges"] = gauge_entries;

        nlohmann::json hist_entries;
        for (auto &h : m_mgroup->m_histograms) {
            std::stringstream ss;
            ss << h.average() << " / " << h.percentile(50) << " / " << h.percentile(95)  << " / " << h.percentile(99);
            std::string desc = h.desc();
            if (!h.subType().empty()) desc = desc + " - " + h.subType();
            hist_entries[desc] = ss.str();
        }
        json["Histograms percentiles (usecs) avg/50/95/99"] = hist_entries;

        return json;
    }

    std::string getJSONString() const {
        return getJSON().dump();
    }

private:
    MetricsGroup* m_mgroup;
    friend class MetricsFarm;
};

class MetricsResult;

class MetricsFarm {
private:
    std::set< MetricsGroupPtr > m_mgroups;
    std::mutex m_lock;

private:
    MetricsFarm() = default;

    [[nodiscard]] auto lock() { return std::lock_guard<decltype(m_lock)>(m_lock); }

public:
    friend class MetricsResult;

    static MetricsFarm& getInstance() {
        static MetricsFarm instance;
        return instance;
    }

    MetricsFarm(const MetricsFarm&) = delete;
    void operator=(const MetricsFarm&) = delete;

    void registerMetricsGroup(MetricsGroupPtr mgroup) {
        assert(mgroup != nullptr);
        auto locked = lock();
        mgroup->on_register();
        m_mgroups.insert(mgroup);
    }

    void deregisterMetricsGroup(MetricsGroupPtr mgroup) {
        assert(mgroup != nullptr);
        auto locked = lock();
        m_mgroups.erase(mgroup);
    }

#if 0
    std::string gather() {
        nlohmann::json json;
        nlohmann::json counter_entries, gauge_entries, hist_entries;

        auto locked = lock();

        /* For each registered mgroup */
         for (auto mgroup : m_mgroups) {
            auto result = mgroup->getResult();
            /* For each registered counter inside the mgroup */
            for (auto const &c : result->m_mgroup->m_counters) {
                std::string desc = mgroup->getName() + "-" + c.name();
                if (!c.subType().empty()) desc = desc + " - " + c.subType();
                counter_entries[desc] = c.get();
            }
            /* For each registered gauge inside the mgroup */
            for (auto const &g : result->m_mgroup->m_gauges) {
                std::string desc = mgroup->getName() + "-" + g.name();
                if (!g.subType().empty()) desc = desc + " - " + g.subType();
                gauge_entries[desc] = g.get();
            }
            /* For each registered histogram inside the mgroup */
            for (auto const &h : result->m_mgroup->m_histograms) {
                std::stringstream ss;
                ss << h.average()   << " / " << h.percentile(50)
                                    << " / " << h.percentile(95)
                                    << " / " << h.percentile(99);
                std::string desc = mgroup->getName() + "-" + h.name();
                if (!h.subType().empty()) desc = desc + " - " + h.subType();
                hist_entries[desc] = ss.str();
            }
        }
        json["Counters"] = counter_entries;
        json["Gauges"] = gauge_entries;
        json["Histograms percentiles (usecs) avg/50/95/99"] = hist_entries;

        return json.dump();
    }
#endif

    std::unique_ptr< MetricsResult > getResult() const {
        return std::make_unique<MetricsResult >(*this);
    }

    nlohmann::json getResultInJSON() const;
    std::string getResultInJSONString() const { return getResultInJSON().dump(); }
};

class MetricsResult {
private:
    std::vector< std::unique_ptr< MetricsGroupResult > > m_result;

public:
    explicit MetricsResult(const MetricsFarm& farm) {
        m_result.reserve(farm.m_mgroups.size());
        for (auto mgroup : farm.m_mgroups) {
            m_result.emplace_back(std::move(mgroup->getResult()));
        }
    }

    nlohmann::json getJSON() const {
        nlohmann::json json;
        for (auto &r : m_result) {
            json[r->m_mgroup->getName()] = r->getJSON();
        }
        return json;
    }

    std::string getJSONString() const {
        return getJSON().dump();
    }
};

nlohmann::json MetricsFarm::getResultInJSON() const {
    nlohmann::json json;
    for (auto &mgroup : m_mgroups) {
        MetricsGroupResult grp_result(mgroup.get(), *(mgroup->m_buffer));
        json[mgroup->getName()] = grp_result.getJSON();
    }
    return json;
}

////////////////////////////////////////// Helper Routine section /////////////////////////////////////////////
template <char... chars>
using tstring = std::integer_sequence<char, chars...>;

template <typename T, T... chars>
constexpr tstring<chars...> operator""_tstr() { return { }; }

template <typename>
struct NamedCounter;

template <typename>
struct NamedGauge;

template <typename>
struct NamedHistogram;

template <char... elements>
struct NamedCounter<tstring<elements...>> {
public:
    static constexpr char Name[sizeof...(elements) + 1] = { elements..., '\0' };
    int m_index;

    static NamedCounter& getInstance() {
        static NamedCounter instance;
        return instance;
    }

    ReportCounter create(const std::string& desc, const std::string& sub_type = "", _publish_as ptype = publish_as_counter) {
        return ReportCounter(std::string(Name), desc, sub_type, ptype);
    }

    const char *getName() const {return Name; }
};

template <char... elements>
struct NamedGauge<tstring<elements...>> {
public:
    static constexpr char Name[sizeof...(elements) + 1] = { elements..., '\0' };
    int m_index;

    static NamedGauge& getInstance() {
        static NamedGauge instance;
        return instance;
    }

    ReportGauge create(const std::string& desc, const std::string& sub_type = "") {
        return ReportGauge(std::string(Name), desc, sub_type);
    }

    const char *getName() const {return Name; }
};

template <char... elements>
struct NamedHistogram<tstring<elements...>> {
public:
    static constexpr char Name[sizeof...(elements) + 1] = { elements..., '\0' };
    int m_index;

    static NamedHistogram& getInstance() {
        static NamedHistogram instance;
        return instance;
    }

    ReportHistogram create(const std::string& desc, const std::string& sub_type = "",
                           const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets)) {
        return ReportHistogram(std::string(Name), desc, sub_type, bkt_boundaries);
    }

    const char *getName() const {return Name; }
};

class MetricsGroupWrapper : public MetricsGroupPtr {
public:
    explicit MetricsGroupWrapper(const char *grp_name) : MetricsGroupPtr(std::make_shared<MetricsGroup>(grp_name)) {}
    void register_me_to_farm() { MetricsFarm::getInstance().registerMetricsGroup(*this); }
};

#define REGISTER_COUNTER(name, ...) \
    { \
        auto &nc = NamedCounter<decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr))>::getInstance(); \
        auto rc = nc.create(__VA_ARGS__); \
        nc.m_index = this->get()->registerCounter(rc); \
    }

#define REGISTER_GAUGE(name, ...) \
    { \
        auto &ng = NamedGauge<decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr))>::getInstance(); \
        auto rg = ng.create(__VA_ARGS__); \
        ng.m_index = this->get()->registerGauge(rg); \
    }

#define REGISTER_HISTOGRAM(name, ...) \
    { \
        auto &nh = NamedHistogram<decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr))>::getInstance(); \
        auto rh = nh.create(__VA_ARGS__); \
        nh.m_index = this->get()->registerHistogram(rh); \
    }

#define METRIC_NAME_TO_INDEX(name) (NamedCounter<decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr))>::getInstance().m_index)
#define COUNTER_INCREMENT(group, name, ...) (group->counterIncrement(METRIC_NAME_TO_INDEX(name), __VA_ARGS__))
#define COUNTER_DECREMENT(group, name, ...) (group->counterDecrement(METRIC_NAME_TO_INDEX(name), __VA_ARGS__))
#define GAUGE_UPDATE(group, name, ...) (group->gaugeUpdate(METRIC_NAME_TO_INDEX(name), __VA_ARGS__))
#define HISTOGRAM_OBSERVE(group, name, ...) (group->histogramObserve(METRIC_NAME_TO_INDEX(name), __VA_ARGS__))

#if 0
#define COUNTER_INCREMENT(group, name, ...) { \
    auto& nc = NamedCounter<decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr))>::getInstance(); \
    std::cout << "Counter accessed for name = " << nc.getName() << " ptr = " << (void *)&nc << " index = " << nc.m_index << "\n"; \
    group->counterIncrement(nc.m_index, __VA_ARGS__); \
}
#endif

} } // namespace sisl::metrics
