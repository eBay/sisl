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
#include <cassert>
#include <typeinfo>
#include <iostream>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <cstring>
#if defined(__linux__) || defined(__APPLE__)
#include <cxxabi.h>
#endif
#include <metrics/metrics.hpp>

namespace sisl {

#ifdef _PRERELEASE
class ObjCounterMetrics;
class ObjCounterRegistry {
public:
    using pair_of_atomic_ptrs = std::pair< std::atomic< int64_t >*, std::atomic< int64_t >* >;

private:
    std::unordered_map< std::string, pair_of_atomic_ptrs > m_tracker_map;
    std::unique_ptr< ObjCounterMetrics > m_metrics;
    // Get a ThreadRegsitry instance to ensure threadregistry is destructed only after us.
    std::shared_ptr< ThreadRegistry > m_treg{ThreadRegistry::get_instance_ptr()};

public:
    static ObjCounterRegistry& inst() {
        static ObjCounterRegistry instance;
        return instance;
    }

    static decltype(m_tracker_map)& tracker() { return inst().m_tracker_map; }

    static void register_obj(const char* name, pair_of_atomic_ptrs ptrs) { tracker()[std::string(name)] = ptrs; }

    static void foreach (const std::function< void(const std::string&, int64_t, int64_t) >& closure) {
        for (auto& e : ObjCounterRegistry::tracker()) {
            closure(e.first, e.second.first->load(std::memory_order_acquire),
                    e.second.second->load(std::memory_order_acquire));
        }
    }

    static ObjCounterMetrics* metrics() { return inst().m_metrics.get(); }
    static inline void enable_metrics_reporting();
};

class ObjCounterMetrics : public MetricsGroup {
public:
    ObjCounterMetrics(const std::vector< std::string >& v) : MetricsGroup("ObjectLife", "Singleton") {
        for (const auto& name : v) {
            // Transform the name to prometheus acceptable name,
            std::string prom_name{name};
            std::transform(prom_name.begin(), prom_name.end(), prom_name.begin(), [](unsigned char c) -> unsigned char {
                if (c == '<' || c == '>' || c == ',' || c == '(' || c == ')' || c == ' ') {
                    return '_';
                } else if (c == '*') {
                    return 'P';
                } else {
                    return c;
                }
            });

            const auto idx{
                this->m_impl_ptr->register_gauge(prom_name, prom_name + " created", prom_name, {"type", "created"})};
            const auto nidx{
                this->m_impl_ptr->register_gauge(prom_name, prom_name + " alive", prom_name, {"type", "alive"})};
            assert(nidx == idx + 1);
            m_name_gauge_map.emplace(name, std::make_pair(idx, nidx));
        }
        register_me_to_farm();
        attach_gather_cb(std::bind(&ObjCounterMetrics::on_gather, this));
    }
    ~ObjCounterMetrics() { deregister_me_from_farm(); }

    void on_gather() {
        ObjCounterRegistry::foreach ([this](const std::string& name, const int64_t created, const int64_t alive) {
            const auto it{m_name_gauge_map.find(name)};
            if (it != m_name_gauge_map.cend()) {
                const auto [create_idx, alive_idx] = it->second;
                this->m_impl_ptr->gauge_update(create_idx, created);
                this->m_impl_ptr->gauge_update(alive_idx, alive);
            }
        });
    }

private:
    std::unordered_map< std::string, std::pair< uint64_t, uint64_t > > m_name_gauge_map;
};

inline void ObjCounterRegistry::enable_metrics_reporting() {
    auto& t{tracker()};
    std::vector< std::string > v;
    std::transform(t.begin(), t.end(), std::back_inserter(v), [](const auto& p) { return p.first; });
    inst().m_metrics = std::make_unique< ObjCounterMetrics >(v);
}

template < typename T >
struct ObjTypeWrapper {
    ObjTypeWrapper(std::atomic< int64_t >* pc, std::atomic< int64_t >* pa) {
        int status{-1};
        char* realname{nullptr};
#if defined(__linux__) || defined(__APPLE__)
        realname = abi::__cxa_demangle(typeid(T).name(), 0, 0, &status);
#endif
        if (status == 0) {
            ObjCounterRegistry::register_obj(realname, std::make_pair(pc, pa));
            std::free(realname);
        } else {
            ObjCounterRegistry::register_obj(typeid(T).name(), std::make_pair(pc, pa));
        }
    }
    int m_dummy; // Dummy value initialized to trigger the registratrion
};

template < typename T >
struct ObjLifeCounter {
    ObjLifeCounter() {
        s_created.fetch_add(1, std::memory_order_relaxed);
        s_alive.fetch_add(1, std::memory_order_relaxed);
        s_type.m_dummy = 0; // To keep s_type initialized during compile time
    }

    ~ObjLifeCounter() {
        assert(s_alive.load() > 0);
        s_alive.fetch_sub(1, std::memory_order_relaxed);
    }

    ObjLifeCounter(const ObjLifeCounter& o) noexcept { s_alive.fetch_add(1, std::memory_order_relaxed); }
    static std::atomic< int64_t > s_created;
    static std::atomic< int64_t > s_alive;
    static ObjTypeWrapper< T > s_type;
};

template < typename T >
std::atomic< int64_t > ObjLifeCounter< T >::s_created(0);

template < typename T >
std::atomic< int64_t > ObjLifeCounter< T >::s_alive(0);

template < typename T >
ObjTypeWrapper< T > ObjLifeCounter< T >::s_type(&ObjLifeCounter< T >::s_created, &ObjLifeCounter< T >::s_alive);

#else

template < typename DS >
struct ObjLifeCounter {};
class ObjCounterRegistry {
public:
    using pair_of_atomic_ptrs = std::pair< std::atomic< int64_t >*, std::atomic< int64_t >* >;

    static ObjCounterRegistry& inst() {
        static ObjCounterRegistry instance;
        return instance;
    }

    static void register_obj(const char* name, pair_of_atomic_ptrs ptrs) {}

    static void foreach (const std::function< void(const std::string&, int64_t, int64_t) >& closure) {}
    static inline void enable_metrics_reporting() {}
};
#endif // _PRERELEASE

} // namespace sisl
