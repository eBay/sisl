/*
 * Created by Hari Kadayam, Sounak Gupta on Dec-12 2018
 *
 */
#pragma once


#include <atomic>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include "boost/preprocessor/cat.hpp"
#include "boost/preprocessor/control/if.hpp"
#include "boost/preprocessor/facilities/expand.hpp"
#include "boost/preprocessor/stringize.hpp"

#include "metrics_atomic.hpp"
#include "metrics_rcu.hpp"
#include "metrics_tlocal.hpp"
#include "nlohmann/json.hpp"
#include "sds_logging/logging.h"
#include "sds_options/options.h"


// TODO: Commenting out this tempoarily till the SDS_OPTIONS and SDS_LOGGING issue is resolved
// SDS_LOGGING_DECL(vmod_metrics_framework)

namespace sisl {

class MetricsGroup {
public:
    MetricsGroup(const MetricsGroup&) = delete;
    MetricsGroup(MetricsGroup&&) noexcept = delete;
    MetricsGroup& operator=(const MetricsGroup&) = delete;
    MetricsGroup& operator=(MetricsGroup&&) noexcept = delete;

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

    [[nodiscard]] auto lock() { return std::lock_guard< decltype(m_lock) >(m_lock); }

public:
    ~MetricsFarm();
    MetricsFarm(const MetricsFarm&) = delete;
    MetricsFarm(MetricsFarm&&) noexcept = delete;
    MetricsFarm& operator=(const MetricsFarm&) = delete;
    MetricsFarm& operator=(MetricsFarm&&) noexcept = delete;

    static MetricsFarm& getInstance();
    static Reporter& get_reporter();
    static bool is_initialized();

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

namespace sisl {

class NamedCounter {
public:
    NamedCounter(const NamedCounter&) = delete;
    NamedCounter(NamedCounter&&) noexcept = delete;
    NamedCounter& operator=(const NamedCounter&) = delete;
    NamedCounter& operator=(NamedCounter&&) noexcept = delete;

    static NamedCounter& getInstance(const std::string& name) {
        const auto insert_pair{s_Counters.try_emplace(name, std::unique_ptr< NamedCounter >{new NamedCounter{name}})};
        return *(insert_pair.first->second.get());
    }

    CounterInfo create(const std::string& instance_name, const std::string& desc, const std::string& report_name = "",
                       const metric_label& label_pair = {"", ""}, _publish_as ptype = sisl::publish_as_counter) {
        return CounterInfo(m_Name, desc, instance_name, report_name, label_pair, ptype);
    }

    CounterInfo create(const std::string& instance_name, const std::string& desc, sisl::_publish_as ptype) {
        return CounterInfo(m_Name, desc, instance_name, "", {"", ""}, ptype);
    }

    void set_index(const uint64_t index) { m_Index = index; }
    [[nodiscard]] uint64_t get_index() const { return m_Index; }
    [[nodiscard]] const std::string& get_name() const { return m_Name; }

private:
    NamedCounter(const std::string& name) : m_Name{name}, m_Index{std::numeric_limits< uint64_t >::max()} {}

    std::string m_Name;
    size_t m_Index;
    static std::unordered_map< std::string, std::unique_ptr< NamedCounter > > s_Counters;
};

class NamedGauge {
public:
    NamedGauge(const NamedGauge&) = delete;
    NamedGauge(NamedGauge&&) noexcept = delete;
    NamedGauge& operator=(const NamedGauge&) = delete;
    NamedGauge& operator=(NamedGauge&&) noexcept = delete;

    static NamedGauge& getInstance(const std::string& name) {
        const auto insert_pair{s_Gauges.try_emplace(name, std::unique_ptr< NamedGauge >{new NamedGauge{name}})};
        return *(insert_pair.first->second.get());
    }

    GaugeInfo create(const std::string& instance_name, const std::string& desc, const std::string& report_name = "",
                     const metric_label& label_pair = {"", ""}) {
        return GaugeInfo(m_Name, desc, instance_name, report_name, label_pair);
    }

    void set_index(const uint64_t index) { m_Index = index; }
    [[nodiscard]] uint64_t get_index() const { return m_Index; }
    [[nodiscard]] const std::string& get_name() const { return m_Name; }

private:
    NamedGauge(const std::string& name) : m_Name{name}, m_Index{std::numeric_limits< uint64_t >::max()} {}

    std::string m_Name;
    size_t m_Index;
    static std::unordered_map< std::string, std::unique_ptr< NamedGauge > > s_Gauges;
};

class NamedHistogram {
public:
    NamedHistogram(const NamedHistogram&) = delete;
    NamedHistogram(NamedHistogram&&) noexcept = delete;
    NamedHistogram& operator=(const NamedHistogram&) = delete;
    NamedHistogram& operator=(NamedHistogram&&) noexcept = delete;

    static NamedHistogram& getInstance(const std::string& name) {
        const auto insert_pair{
            s_Histograms.try_emplace(name, std::unique_ptr< NamedHistogram >{new NamedHistogram{name}})};
        return *(insert_pair.first->second.get());
    }

    HistogramInfo create(const std::string& instance_name, const std::string& desc, const std::string& report_name = "",
                         const metric_label& label_pair = {"", ""},
                         const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets)) {
        return HistogramInfo(m_Name, desc, instance_name, report_name, label_pair, bkt_boundaries);
    }

    sisl::HistogramInfo create(const std::string& instance_name, const std::string& desc,
                               const sisl::hist_bucket_boundaries_t& bkt_boundaries) {
        return sisl::HistogramInfo(m_Name, desc, instance_name, "", {"", ""}, bkt_boundaries);
    }

    void set_index(const uint64_t index) { m_Index = index; }
    [[nodiscard]] uint64_t get_index() const { return m_Index; }
    [[nodiscard]] const std::string& get_name() const { return m_Name; }

private:
    NamedHistogram(const std::string& name) : m_Name{name}, m_Index{std::numeric_limits< uint64_t >::max()} {}

    std::string m_Name;
    size_t m_Index;
    static std::unordered_map< std::string, std::unique_ptr< NamedHistogram > > s_Histograms;
};

#define REGISTER_COUNTER(name, ...)                                                                                    \
    {                                                                                                                  \
        auto& nc{sisl::NamedCounter::getInstance(BOOST_PP_STRINGIZE(name))};                                           \
        auto rc{nc.create(this->m_impl_ptr->get_instance_name(), __VA_ARGS__)};                                        \
        nc.set_index(this->m_impl_ptr->register_counter(rc));                                                          \
    }

#define REGISTER_GAUGE(name, ...)                                                                                      \
    {                                                                                                                  \
        auto& ng{sisl::NamedGauge::getInstance(BOOST_PP_STRINGIZE(name))};                                             \
        auto rg{ng.create(this->m_impl_ptr->get_instance_name(), __VA_ARGS__)};                                        \
        ng.set_index(this->m_impl_ptr->register_gauge(rg));                                                            \
    }

#define REGISTER_HISTOGRAM(name, ...)                                                                                  \
    {                                                                                                                  \
        auto& nh{sisl::NamedHistogram::getInstance(BOOST_PP_STRINGIZE(name))};                                         \
        auto rh{nh.create(this->m_impl_ptr->get_instance_name(), __VA_ARGS__)};                                        \
        nh.set_index(this->m_impl_ptr->register_histogram(rh));                                                        \
    }

#define COUNTER_INDEX(name) METRIC_NAME_TO_INDEX(NamedCounter, name)
#define GAUGE_INDEX(name) METRIC_NAME_TO_INDEX(NamedGauge, name)
#define HISTOGRAM_INDEX(name) METRIC_NAME_TO_INDEX(NamedHistogram, name)

#define METRIC_NAME_TO_INDEX(type, name)                                                                              \
    (sisl::type::getInstance(BOOST_PP_STRINGIZE(name)).get_index())


// TODO: Replace printf and #ifnef DEBUG with DLOGDFATAL_IF once the issue of SDS_LOGGING and SDS_OPTIONS are resolved

#ifndef NDEBUG
#define __VALIDATE_AND_EXECUTE(group, type, method, name, ...)                                                         \
    {                                                                                                                  \
        const auto index{ METRIC_NAME_TO_INDEX(type, name) };                                                                     \
        if (index == std::numeric_limits<decltype(index)>::max()) {                                                                                                 \
            fprintf(stderr, "Metric name '%s' not registered yet but used\n", BOOST_PP_STRINGIZE(name));               \
            fflush(stderr);                                                                                            \
            assert(0);                                                                                                 \
        }                                                                                                              \
        ((group).m_impl_ptr->method(index, __VA_ARGS__));                                                                  \
    }
#else
#define __VALIDATE_AND_EXECUTE(group, type, method, name, ...)                                                         \
    {                                                                                                                  \
        const auto index{METRIC_NAME_TO_INDEX(type, name)};                                                                     \
        ((group).m_impl_ptr->method(index, __VA_ARGS__));                                                                  \
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
        const auto& nc{NamedCounter::getInstance(BOOST_PP_STRINGIZE(name))};                                                 \
        std::cout << "Counter accessed for name = " << nc.get_name() << " ptr = " << (void*)&nc                        \
                  << " index = " << nc.get_index() << "\n";                                                            \
        group->counterIncrement(nc.get_index(), __VA_ARGS__);                                                              \
    }
#endif

} // namespace sisl
