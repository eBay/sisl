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
#include "prometheus_reporter.hpp"

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
    CounterInfo(const std::string& name, const std::string& desc, const std::string& instance_name,
                _publish_as ptype = publish_as_counter);
    CounterInfo(const std::string& name, const std::string& desc, const std::string& instance_name,
                const std::string& report_name, const metric_label& label_pair, _publish_as ptype = publish_as_counter);

    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string label_pair() const {
        return (!m_label_pair.first.empty() && !m_label_pair.second.empty())
            ? m_label_pair.first + "-" + m_label_pair.second
            : "";
    }
    void publish(const CounterValue& value);

private:
    std::string                           m_name;
    std::string                           m_desc;
    std::pair< std::string, std::string > m_label_pair;
    std::shared_ptr< ReportCounter >      m_report_counter;
    std::shared_ptr< ReportGauge >        m_report_gauge;
};

class GaugeInfo {
    friend class MetricsGroup;

public:
    GaugeInfo(const std::string& name, const std::string& desc, const std::string& instance_name,
              const std::string& report_name = "", const metric_label& label_pair = {"", ""});

    uint64_t    get() const { return m_gauge.get(); };
    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string label_pair() const {
        return (!m_label_pair.first.empty() && !m_label_pair.second.empty())
            ? m_label_pair.first + "-" + m_label_pair.second
            : "";
    }
    void publish();

private:
    GaugeValue                            m_gauge;
    const std::string                     m_name;
    const std::string                     m_desc;
    std::pair< std::string, std::string > m_label_pair;
    std::shared_ptr< ReportGauge >        m_report_gauge;
};

class HistogramInfo {
public:
    HistogramInfo(const std::string& name, const std::string& desc, const std::string& instance_name,
                  const std::string& report_name = "", const metric_label& label_pair = {"", ""},
                  const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets));

    double  percentile(const HistogramValue& hvalue, float pcntl) const;
    int64_t count(const HistogramValue& hvalue) const;
    double  average(const HistogramValue& hvalue) const;

    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string label_pair() const {
        return (!m_label_pair.first.empty() && !m_label_pair.second.empty())
            ? m_label_pair.first + "-" + m_label_pair.second
            : "";
    }

    void                            publish(const HistogramValue& hvalue);
    const hist_bucket_boundaries_t& get_boundaries() const { return m_bkt_boundaries; }

private:
    const std::string                     m_name;
    const std::string                     m_desc;
    std::pair< std::string, std::string > m_label_pair;
    const hist_bucket_boundaries_t&       m_bkt_boundaries;
    std::shared_ptr< ReportHistogram >    m_report_histogram;
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

        memset((void*)m_counters, 0, (sizeof(CounterValue) * ncntrs));
        memset((void*)m_histograms, 0, (sizeof(HistogramValue) * nhists));

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
    static MetricsGroupPtr make_group(const char* grp_name, const char* inst_name);

    MetricsGroup(const char* grp_name, const char* inst_name);
    MetricsGroup(const std::string& grp_name, const std::string& inst_name);

    uint64_t register_counter(const std::string& name, const std::string& desc, const std::string& report_name = "",
                              const metric_label& label_pair = {"", ""}, _publish_as ptype = publish_as_counter);
    uint64_t register_counter(const std::string& name, const std::string& desc, _publish_as ptype);

    uint64_t register_counter(const CounterInfo& counter);

    uint64_t register_gauge(const std::string& name, const std::string& desc, const std::string& report_name = "",
                            const metric_label& label_pair = {"", ""});
    uint64_t register_gauge(const GaugeInfo& gauge);

    uint64_t register_histogram(const std::string& name, const std::string& desc, const std::string& report_name = "",
                                const metric_label& label_pair = {"", ""},
                                const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets));
    uint64_t register_histogram(HistogramInfo& hist);
    uint64_t register_histogram(const std::string& name, const std::string& desc,
                                const hist_bucket_boundaries_t& bkt_boundaries);

    void counter_increment(uint64_t index, int64_t val = 1);
    void counter_decrement(uint64_t index, int64_t val = 1);
    void gauge_update(uint64_t index, int64_t val);
    void histogram_observe(uint64_t index, int64_t val);

    const CounterInfo&   get_counter_info(uint64_t index) const;
    const GaugeInfo&     get_gauge_info(uint64_t index) const;
    const HistogramInfo& get_histogram_info(uint64_t index) const;

    nlohmann::json get_result_in_json(bool need_latest);
    void           prepare_gather();

    void               publish_result();
    const std::string& get_group_name() const;
    const std::string& get_instance_name() const;

private:
    void on_register();

    void gather_result(bool need_latest, std::function< void(CounterInfo&, const CounterValue&) > counter_cb,
                       std::function< void(GaugeInfo&) >                            gauge_cb,
                       std::function< void(HistogramInfo&, const HistogramValue&) > histogram_cb);

private:
    std::string                          m_grp_name;
    std::string                          m_inst_name;
    std::mutex                           m_mutex;
    std::unique_ptr< ThreadSafeMetrics > m_metrics;
    bool                                 m_gather_pending;

    std::vector< CounterInfo >                                              m_counters;
    std::vector< GaugeInfo >                                                m_gauges;
    std::vector< HistogramInfo >                                            m_histograms;
    std::vector< std::reference_wrapper< const hist_bucket_boundaries_t > > m_bkt_boundaries;
};

class MetricsFarm {
private:
    std::set< MetricsGroupPtr > m_mgroups;
    std::mutex                  m_lock;
    std::unique_ptr< Reporter > m_reporter;

private:
    MetricsFarm();

    [[nodiscard]] auto lock() { return std::lock_guard< decltype(m_lock) >(m_lock); }

public:
    friend class MetricsResult;

    static MetricsFarm& getInstance();
    static Reporter&    get_reporter();

    MetricsFarm(const MetricsFarm&) = delete;
    void operator=(const MetricsFarm&) = delete;

    void register_metrics_group(MetricsGroupPtr mgroup);
    void deregister_metrics_group(MetricsGroupPtr mgroup);

    nlohmann::json get_result_in_json(bool need_latest = true);
    std::string    get_result_in_json_string(bool need_latest = true);
    std::string    report(ReportFormat format);
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

    CounterInfo create(const std::string& instance_name, const std::string& desc, const std::string& report_name = "",
                       const metric_label& label_pair = {"", ""}, _publish_as ptype = publish_as_counter) {
        return CounterInfo(std::string(Name), desc, instance_name, report_name, label_pair, ptype);
    }
    CounterInfo create(const std::string& instance_name, const std::string& desc, _publish_as ptype) {
        return CounterInfo(std::string(Name), desc, instance_name, "", {"", ""}, ptype);
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

    GaugeInfo create(const std::string& instance_name, const std::string& desc, const std::string& report_name = "",
                     const metric_label& label_pair = {"", ""}) {
        return GaugeInfo(std::string(Name), desc, instance_name, report_name, label_pair);
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

    HistogramInfo create(const std::string& instance_name, const std::string& desc, const std::string& report_name = "",
                         const metric_label&             label_pair = {"", ""},
                         const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets)) {
        return HistogramInfo(std::string(Name), desc, instance_name, report_name, label_pair, bkt_boundaries);
    }

    HistogramInfo create(const std::string& instance_name, const std::string& desc,
                         const hist_bucket_boundaries_t& bkt_boundaries) {
        return HistogramInfo(std::string(Name), desc, instance_name, "", {"", ""}, bkt_boundaries);
    }

    const char* get_name() const { return Name; }
};

class MetricsGroupWrapper : public MetricsGroupPtr {
public:
    MetricsGroupWrapper(const char* grp_name, const char *inst_name = "Instance1") :
        MetricsGroupPtr(std::make_shared< MetricsGroup >(grp_name, inst_name)) {}

    MetricsGroupWrapper(const std::string& grp_name, const std::string& inst_name = "Instance1") :
        MetricsGroupPtr(std::make_shared< MetricsGroup >(grp_name, inst_name)) {}

    void register_me_to_farm() { MetricsFarm::getInstance().register_metrics_group(*this); }
};

#define REGISTER_COUNTER(name, ...)                                                                                    \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        auto& nc = sisl::NamedCounter< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance();       \
        auto  rc = nc.create(this->get()->get_instance_name(), __VA_ARGS__);                                                    \
        nc.m_index = this->get()->register_counter(rc);                                                                \
    }

#define REGISTER_GAUGE(name, ...)                                                                                      \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        auto& ng = sisl::NamedGauge< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance();         \
        auto  rg = ng.create(this->get()->get_instance_name(), __VA_ARGS__);                                                    \
        ng.m_index = this->get()->register_gauge(rg);                                                                  \
    }

#define REGISTER_HISTOGRAM(name, ...)                                                                                  \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        auto& nh = sisl::NamedHistogram< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance();     \
        auto  rh = nh.create(this->get()->get_instance_name(), __VA_ARGS__);                                                    \
        nh.m_index = this->get()->register_histogram(rh);                                                             \
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
