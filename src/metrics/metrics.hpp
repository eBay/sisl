/*
 * Created by Hari Kadayam, Sounak Gupta on Dec-12 2018
 *
 */
#pragma once

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/facilities/expand.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <sds_logging/logging.h>
#include <sds_options/options.h>
#include <atomic>
#include <set>
#include <mutex>
#include <nlohmann/json.hpp>
#include "metrics_tlocal.hpp"
#include "metrics_rcu.hpp"
#include "metrics_atomic.hpp"

// TODO: Commenting out this tempoarily till the SDS_OPTIONS and SDS_LOGGING issue is resolved
// SDS_LOGGING_DECL(vmod_metrics_framework)

namespace sisl {

class MetricsGroup {
public:
    MetricsGroupImplPtr m_impl_ptr;

    std::atomic< bool > m_is_registered = false;
    static MetricsGroupImplPtr make_group(const std::string& grp_name, const std::string& inst_name,
                                          group_impl_type_t type = group_impl_type_t::rcu);

    MetricsGroup(const std::string& grp_name, const std::string& inst_name = "Instance1",
                 group_impl_type_t type = group_impl_type_t::rcu) {
        m_impl_ptr = make_group(grp_name, inst_name, type);
    }

    ~MetricsGroup() { deregister_me_from_farm(); }

    void register_me_to_farm();
    void deregister_me_from_farm();

    nlohmann::json get_result_in_json(bool need_latest);
    void gather();
    void attach_gather_cb(const on_gather_cb_t& cb);
    void detach_gather_cb();
};

class MetricsFarm {
private:
    std::set< MetricsGroupImplPtr > m_mgroups;
    std::mutex m_lock;
    std::unique_ptr< Reporter > m_reporter;

private:
    MetricsFarm();
    ~MetricsFarm();

    [[nodiscard]] auto lock() { return std::lock_guard< decltype(m_lock) >(m_lock); }

public:
    static MetricsFarm& getInstance();
    static Reporter& get_reporter();
    static bool is_initialized();

    MetricsFarm(const MetricsFarm&) = delete;
    void operator=(const MetricsFarm&) = delete;

    void register_metrics_group(MetricsGroupImplPtr mgroup);
    void deregister_metrics_group(MetricsGroupImplPtr mgroup);

    nlohmann::json get_result_in_json(bool need_latest = true);
    std::string get_result_in_json_string(bool need_latest = true);
    std::string report(ReportFormat format);
    void gather(); // Dummy call just to make it gather. Does not report
};

using MetricsGroupWrapper = MetricsGroup; // For backward compatibility reasons

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
    int m_index = -1;

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
    int m_index = -1;

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
    int m_index = -1;

    static NamedHistogram& getInstance() {
        static NamedHistogram instance;
        return instance;
    }

    HistogramInfo create(const std::string& instance_name, const std::string& desc, const std::string& report_name = "",
                         const metric_label& label_pair = {"", ""},
                         const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets)) {
        return HistogramInfo(std::string(Name), desc, instance_name, report_name, label_pair, bkt_boundaries);
    }

    HistogramInfo create(const std::string& instance_name, const std::string& desc,
                         const hist_bucket_boundaries_t& bkt_boundaries) {
        return HistogramInfo(std::string(Name), desc, instance_name, "", {"", ""}, bkt_boundaries);
    }

    const char* get_name() const { return Name; }
};

#define REGISTER_COUNTER(name, ...)                                                                                    \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        auto& nc = sisl::NamedCounter< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance();       \
        auto rc = nc.create(this->m_impl_ptr->get_instance_name(), __VA_ARGS__);                                       \
        nc.m_index = this->m_impl_ptr->register_counter(rc);                                                           \
    }

#define REGISTER_GAUGE(name, ...)                                                                                      \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        auto& ng = sisl::NamedGauge< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance();         \
        auto rg = ng.create(this->m_impl_ptr->get_instance_name(), __VA_ARGS__);                                       \
        ng.m_index = this->m_impl_ptr->register_gauge(rg);                                                             \
    }

#define REGISTER_HISTOGRAM(name, ...)                                                                                  \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        auto& nh = sisl::NamedHistogram< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance();     \
        auto rh = nh.create(this->m_impl_ptr->get_instance_name(), __VA_ARGS__);                                       \
        nh.m_index = this->m_impl_ptr->register_histogram(rh);                                                         \
    }

#define COUNTER_INDEX(name) METRIC_NAME_TO_INDEX(NamedCounter, name)
#define GAUGE_INDEX(name) METRIC_NAME_TO_INDEX(NamedGauge, name)
#define HISTOGRAM_INDEX(name) METRIC_NAME_TO_INDEX(NamedHistogram, name)

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
        ((group).m_impl_ptr->method(i, __VA_ARGS__));                                                                  \
    }
#else
#define __VALIDATE_AND_EXECUTE(group, type, method, name, ...)                                                         \
    {                                                                                                                  \
        auto i = METRIC_NAME_TO_INDEX(type, name);                                                                     \
        ((group).m_impl_ptr->method(i, __VA_ARGS__));                                                                  \
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
