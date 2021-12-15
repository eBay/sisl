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
#include "logging/logging.h"
#include "options/options.h"

#include "metrics_atomic.hpp"
#include "metrics_group_impl.hpp"
#include "metrics_rcu.hpp"
#include "metrics_tlocal.hpp"

// TODO: Commenting out this tempoarily till the SISL_OPTIONS and SISL_LOGGING issue is resolved
// SISL_LOGGING_DECL(vmod_metrics_framework)

namespace sisl {

class MetricsGroupStaticInfo;
class MetricsFarm;
class MetricsGroup {
public:
    MetricsGroup(const std::string& grp_name, const std::string& inst_name = "Instance1",
                 group_impl_type_t type = group_impl_type_t::rcu);
    ~MetricsGroup();

    MetricsGroup(const MetricsGroup&) = delete;
    MetricsGroup(MetricsGroup&&) noexcept = delete;
    MetricsGroup& operator=(const MetricsGroup&) = delete;
    MetricsGroup& operator=(MetricsGroup&&) noexcept = delete;

    MetricsGroupImplPtr m_impl_ptr;
    std::shared_ptr< MetricsFarm > m_farm_ptr; // Take reference to prevent from singleton destruction before

    std::atomic< bool > m_is_registered = false;
    static MetricsGroupImplPtr make_group(const std::string& grp_name, const std::string& inst_name,
                                          group_impl_type_t type = group_impl_type_t::rcu);

    void register_me_to_farm();
    void deregister_me_from_farm();
    void register_me_to_parent(MetricsGroup* parent);

    nlohmann::json get_result_in_json(bool need_latest);
    void gather();
    void attach_gather_cb(const on_gather_cb_t& cb);
    void detach_gather_cb();

    [[nodiscard]] std::string instance_name() const { return m_impl_ptr->instance_name(); }
};

class MetricsFarm {
private:
    std::set< MetricsGroupImplPtr > m_mgroups;
    std::unordered_map< std::string, uint64_t > m_uniq_inst_maintainer;
    mutable std::mutex m_lock;
    std::unique_ptr< Reporter > m_reporter;
    std::shared_ptr< ThreadRegistry > m_treg; // Keep a ref of ThreadRegistry to prevent it from destructing before us.
private:
    MetricsFarm();

    [[nodiscard]] auto lock() const { return std::lock_guard< decltype(m_lock) >(m_lock); }

public:
    ~MetricsFarm();
    MetricsFarm(const MetricsFarm&) = delete;
    MetricsFarm(MetricsFarm&&) noexcept = delete;
    MetricsFarm& operator=(const MetricsFarm&) = delete;
    MetricsFarm& operator=(MetricsFarm&&) noexcept = delete;

    static MetricsFarm& getInstance() { return *get_instance_ptr(); }

    static std::shared_ptr< MetricsFarm > get_instance_ptr() {
        static std::shared_ptr< MetricsFarm > inst_ptr{new MetricsFarm()};
        return inst_ptr;
    }

    static Reporter& get_reporter();
    static bool is_initialized();

    void register_metrics_group(MetricsGroupImplPtr mgroup, const bool add_to_farm_list = true);
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

#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

template < typename T, T... chars >
constexpr tstring< chars... > operator""_tstr() {
    return {};
}

#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic pop
#endif

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

// TODO: Replace printf and #ifnef DEBUG with DLOGDFATAL_IF once the issue of SISL_LOGGING and SISL_OPTIONS are resolved

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
#define GAUGE_UPDATE_IF_ELSE(group, cond, namea, nameb, ...)                                                           \
    __VALIDATE_AND_EXECUTE_IF_ELSE(group, NamedGauge, gauge_update, cond, namea, nameb, __VA_ARGS__)

#define HISTOGRAM_OBSERVE(group, name, ...)                                                                            \
    __VALIDATE_AND_EXECUTE(group, NamedHistogram, histogram_observe, name, __VA_ARGS__)
#define HISTOGRAM_OBSERVE_IF_ELSE(group, cond, namea, nameb, ...)                                                      \
    __VALIDATE_AND_EXECUTE_IF_ELSE(group, NamedHistogram, histogram_observe, cond, namea, nameb, __VA_ARGS__)

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
