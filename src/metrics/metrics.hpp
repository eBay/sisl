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

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/facilities/expand.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <nlohmann/json.hpp>
#include <sds_logging/logging.h>
#include <sds_options/options.h>

#include "metrics_atomic.hpp"
#include "metrics_group_impl.hpp"
#include "metrics_rcu.hpp"
#include "metrics_tlocal.hpp"

// TODO: Commenting out this tempoarily till the SDS_OPTIONS and SDS_LOGGING issue is resolved
// SDS_LOGGING_DECL(vmod_metrics_framework)

namespace sisl {

class MetricsGroupStaticInfo;
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
    std::unordered_map< std::string, uint64_t > m_uniq_inst_maintainer;
    mutable std::mutex m_lock;
    std::unique_ptr< Reporter > m_reporter;

private:
    MetricsFarm();

    [[nodiscard]] auto lock() const { return std::lock_guard< decltype(m_lock) >(m_lock); }

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

    std::string ensure_unique(const std::string& grp_name, const std::string& inst_name);
};

using MetricsGroupWrapper = MetricsGroup; // For backward compatibility reasons

} // namespace sisl

////////////////////////////////////////// Helper Routine section ////////////////////////////////////////////////

namespace sisl {

template < char... chars >
using tstring = std::integer_sequence< char, chars... >;

template < typename T, T... chars >
constexpr tstring< chars... > operator""_tstr() {
    return {};
}

template < typename >
class NamedCounter;

template < typename >
class NamedGauge;

template < typename >
class NamedHistogram;

template < char... elements >
class NamedCounter< tstring< elements... > > {
public:
    NamedCounter(const NamedCounter&) = delete;
    NamedCounter(NamedCounter&&) noexcept = delete;
    NamedCounter& operator=(const NamedCounter&) = delete;
    NamedCounter& operator=(NamedCounter&&) noexcept = delete;

    static NamedCounter& getInstance() {
        static NamedCounter instance{};
        return instance;
    }

    void set_index(const uint64_t index) { m_Index = index; }
    [[nodiscard]] uint64_t get_index() const { return m_Index; }
    [[nodiscard]] constexpr const char* get_name() const { return m_Name; }

private:
    NamedCounter() : m_Index{std::numeric_limits< uint64_t >::max()} {}

    static constexpr char m_Name[sizeof...(elements) + 1] = {elements..., '\0'};
    size_t m_Index;
};

template < char... elements >
class NamedGauge< tstring< elements... > > {
public:
    NamedGauge(const NamedGauge&) = delete;
    NamedGauge(NamedGauge&&) noexcept = delete;
    NamedGauge& operator=(const NamedGauge&) = delete;
    NamedGauge& operator=(NamedGauge&&) noexcept = delete;

    static NamedGauge& getInstance() {
        static NamedGauge instance{};
        return instance;
    }

    void set_index(const uint64_t index) { m_Index = index; }
    [[nodiscard]] uint64_t get_index() const { return m_Index; }
    [[nodiscard]] constexpr const char* get_name() const { return m_Name; }

private:
    NamedGauge() : m_Index{std::numeric_limits< uint64_t >::max()} {}

    static constexpr char m_Name[sizeof...(elements) + 1] = {elements..., '\0'};
    size_t m_Index;
};

template < char... elements >
struct NamedHistogram< tstring< elements... > > {
public:
    NamedHistogram(const NamedHistogram&) = delete;
    NamedHistogram(NamedHistogram&&) noexcept = delete;
    NamedHistogram& operator=(const NamedHistogram&) = delete;
    NamedHistogram& operator=(NamedHistogram&&) noexcept = delete;

    static NamedHistogram& getInstance() {
        static NamedHistogram instance;
        return instance;
    }

    void set_index(const uint64_t index) { m_Index = index; }
    [[nodiscard]] uint64_t get_index() const { return m_Index; }
    [[nodiscard]] constexpr const char* get_name() const { return m_Name; }

private:
    NamedHistogram() : m_Index{std::numeric_limits< uint64_t >::max()} {}

    static constexpr char m_Name[sizeof...(elements) + 1] = {elements..., '\0'};
    size_t m_Index;
};

// decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)

#define REGISTER_COUNTER(name, ...)                                                                                    \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        auto& nc{sisl::NamedCounter< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance()};        \
        nc.set_index(this->m_impl_ptr->register_counter(nc.get_name(), __VA_ARGS__));                                  \
    }

#define REGISTER_GAUGE(name, ...)                                                                                      \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        auto& ng{sisl::NamedGauge< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance()};          \
        ng.set_index(this->m_impl_ptr->register_gauge(ng.get_name(), __VA_ARGS__));                                    \
    }

#define REGISTER_HISTOGRAM(name, ...)                                                                                  \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        auto& nh{sisl::NamedHistogram< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance()};      \
        nh.set_index(this->m_impl_ptr->register_histogram(nh.get_name(), __VA_ARGS__));                                \
    }

#define COUNTER_INDEX(name) METRIC_NAME_TO_INDEX(NamedCounter, name)
#define GAUGE_INDEX(name) METRIC_NAME_TO_INDEX(NamedGauge, name)
#define HISTOGRAM_INDEX(name) METRIC_NAME_TO_INDEX(NamedHistogram, name)

#define METRIC_NAME_TO_INDEX(type, name)                                                                               \
    (sisl::type< decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _tstr)) >::getInstance().get_index())

// TODO: Replace printf and #ifnef DEBUG with DLOGDFATAL_IF once the issue of SDS_LOGGING and SDS_OPTIONS are resolved

#ifndef NDEBUG
#define __VALIDATE_AND_EXECUTE(group, type, method, name, ...)                                                         \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        const auto index{METRIC_NAME_TO_INDEX(type, name)};                                                            \
        if (index == std::numeric_limits< decltype(index) >::max()) {                                                  \
            fprintf(stderr, "Metric name '%s' not registered yet but used\n", BOOST_PP_STRINGIZE(name));               \
            fflush(stderr);                                                                                            \
            assert(0);                                                                                                 \
        }                                                                                                              \
        ((group).m_impl_ptr->method(index, __VA_ARGS__));                                                              \
    }
#else
#define __VALIDATE_AND_EXECUTE(group, type, method, name, ...)                                                         \
    {                                                                                                                  \
        using namespace sisl;                                                                                          \
        const auto index{METRIC_NAME_TO_INDEX(type, name)};                                                            \
        ((group).m_impl_ptr->method(index, __VA_ARGS__));                                                              \
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
        const auto& nc{NamedCounter::getInstance(BOOST_PP_STRINGIZE(name))};                                           \
        std::cout << "Counter accessed for name = " << nc.get_name() << " ptr = " << (void*)&nc                        \
                  << " index = " << nc.get_index() << "\n";                                                            \
        group->counterIncrement(nc.get_index(), __VA_ARGS__);                                                          \
    }
#endif

} // namespace sisl
