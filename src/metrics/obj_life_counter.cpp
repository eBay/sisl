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

#include <cassert>
#include <sisl/utility/obj_life_counter.hpp>

namespace sisl {

// ----- ObjCounterMetrics -----

ObjCounterMetrics::ObjCounterMetrics(const std::vector< std::string >& v) : MetricsGroup("ObjectLife", "Singleton") {
    for (const auto& name : v) {
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
    attach_gather_cb([this]() { on_gather(); });
}

ObjCounterMetrics::~ObjCounterMetrics() { deregister_me_from_farm(); }

void ObjCounterMetrics::on_gather() {
    ObjCounterRegistry::foreach ([this](const std::string& name, const int64_t created, const int64_t alive) {
        const auto it{m_name_gauge_map.find(name)};
        if (it != m_name_gauge_map.cend()) {
            const auto [create_idx, alive_idx] = it->second;
            this->m_impl_ptr->gauge_update(create_idx, created);
            this->m_impl_ptr->gauge_update(alive_idx, alive);
        }
    });
}

// ----- ObjCounterRegistry -----

ObjCounterRegistry::~ObjCounterRegistry() = default;

ObjCounterRegistry& ObjCounterRegistry::inst() {
    static ObjCounterRegistry instance;
    return instance;
}

void ObjCounterRegistry::enable_metrics_reporting() {
    auto& t{tracker()};
    std::vector< std::string > v;
    std::transform(t.begin(), t.end(), std::back_inserter(v), [](const auto& p) { return p.first; });
    inst().m_metrics = std::make_unique< ObjCounterMetrics >(v);
}

} // namespace sisl
