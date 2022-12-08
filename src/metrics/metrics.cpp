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
#include <sisl/logging/logging.h>

#include "sisl/metrics/metrics.hpp"

THREAD_BUFFER_INIT

namespace sisl {

#define PROMETHEUS_METRICS_REPORTER

static std::atomic< bool > metrics_farm_initialized{false};

/**************************** MetricsFarm ***********************************/
Reporter& MetricsFarm::get_reporter() { return *getInstance().m_reporter; }

MetricsFarm::MetricsFarm() {
    metrics_farm_initialized = true;
    m_treg = ThreadRegistry::get_instance_ptr();

#ifdef PROMETHEUS_METRICS_REPORTER
    // m_reporter = std::dynamic_pointer_cast< Reporter >(std::make_unique< PrometheusReporter >());
    m_reporter = std::make_unique< PrometheusReporter >();
#endif
}

MetricsFarm::~MetricsFarm() { metrics_farm_initialized = false; }

bool MetricsFarm::is_initialized() { return metrics_farm_initialized.load(); }

void MetricsFarm::register_metrics_group(MetricsGroupImplPtr mgrp_impl, const bool add_to_farm_list) {
    assert(mgrp_impl != nullptr);
    auto locked{lock()};
    mgrp_impl->on_register();
    if (add_to_farm_list) { m_mgroups.insert(mgrp_impl); }
    mgrp_impl->registration_completed();
}

void MetricsFarm::deregister_metrics_group(MetricsGroupImplPtr mgrp_impl) {
    assert(mgrp_impl != nullptr);
    auto locked{lock()};
    m_mgroups.erase(mgrp_impl);
}

nlohmann::json MetricsFarm::get_result_in_json(bool need_latest) {
    nlohmann::json json;
    auto locked{lock()};
    for (auto& mgroup : m_mgroups) {
        json[mgroup->get_group_name()][mgroup->get_instance_name()] = mgroup->get_result_in_json(need_latest);
    }

    return json;
}

std::string MetricsFarm::get_result_in_json_string(bool need_latest) { return get_result_in_json(need_latest).dump(); }

std::string MetricsFarm::report(ReportFormat format) {
    auto locked{lock()};
    for (auto& mgroup : m_mgroups) {
        mgroup->publish_result();
    }

    // Now everything is published to reporter, serialize them
    return m_reporter->serialize(format);
}

void MetricsFarm::gather() {
    nlohmann::json json;
    auto locked{lock()};
    bool flushed{false};
    for (auto& mgroup : m_mgroups) {
        if (!flushed && (mgroup->impl_type() == group_impl_type_t::thread_buf_signal)) {
            ThreadBufferMetricsGroup::flush_core_cache();
        }
        flushed = true;
        mgroup->gather();
    }
}

////////////////////////////////////////// Helper Routine section ////////////////////////////////////////////////
///
std::string MetricsFarm::ensure_unique(const std::string& grp_name, const std::string& inst_name) {
    auto locked{lock()};

    // If 2 instances are provided with same name (unknowingly), prometheus with same label pairs, return the same
    // prometheus::Counter pointer, which means if one of them freed, other could access it. To prevent that, we
    // are creating a unique instance name, so that we have one per registration.
    const auto it{m_uniq_inst_maintainer.find(grp_name + inst_name)};
    if (it == std::end(m_uniq_inst_maintainer)) {
        m_uniq_inst_maintainer.insert(std::make_pair<>(grp_name + inst_name, 1));
        return inst_name;
    } else {
        ++(it->second);
        return inst_name + "_" + std::to_string(it->second);
    }
}
} // namespace sisl
