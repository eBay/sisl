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

#include <map>
#include <unordered_map>
#include <memory>
#include <string>
#include <mutex>
#include "reporter.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/serializer.h>
#include <prometheus/text_serializer.h>
#pragma GCC diagnostic pop

#include "logging/logging.h"

namespace sisl {

class PrometheusReportCounter : public ReportCounter {
public:
    PrometheusReportCounter(prometheus::Family< prometheus::Counter >& family,
                            const std::map< std::string, std::string >& label_pairs) :
            m_counter(family.Add(label_pairs)) {}

    virtual void set_value(double value) override {
        double counter_value = m_counter.Value();
        double diff = value - counter_value;
        assert(diff >= 0);

        // we rely on prometheus::counter to check whether the passed value is < 0, and if so, discard the passed value
        m_counter.Increment(diff);
    };

    prometheus::Counter& m_counter;
};

class PrometheusReportGauge : public ReportGauge {
public:
    PrometheusReportGauge(prometheus::Family< prometheus::Gauge >& family,
                          const std::map< std::string, std::string >& label_pairs) :
            m_gauge(family.Add(label_pairs)) {}

    virtual void set_value(double value) override { m_gauge.Set(value); };

    prometheus::Gauge& m_gauge;
};

class PrometheusReportHistogram : public ReportHistogram {
public:
    PrometheusReportHistogram(prometheus::Family< prometheus::Histogram >& family,
                              const std::map< std::string, std::string >& label_pairs,
                              const hist_bucket_boundaries_t& bkt_boundaries) :
            m_histogram(family.Add(label_pairs, bkt_boundaries)) {}

    virtual void set_value(const std::vector< double >& bucket_values, double sum) {
        // Use modified prometheus method (not part of original repo)
        m_histogram.TransferBucketCounters(bucket_values, sum);
    }

    prometheus::Histogram& m_histogram;
};

class PrometheusReporter : public Reporter {
public:
    PrometheusReporter() {
        m_registry = std::make_shared< prometheus::Registry >();
        m_serializer = std::unique_ptr< prometheus::Serializer >(new prometheus::TextSerializer());
        m_cur_serializer_format = kTextFormat;
    }

    virtual ~PrometheusReporter() = default;

    std::shared_ptr< ReportCounter > add_counter(const std::string& name, const std::string& desc,
                                                 const std::string& instance_name,
                                                 const metric_label& label_pair = {"", ""}) {
        prometheus::Family< prometheus::Counter >* family_ptr;

        std::unique_lock lk(m_mutex);
        auto it = m_counter_families.find(name);
        if (it == m_counter_families.end()) {
            auto& family = prometheus::BuildCounter().Name(name).Help(desc).Register(*m_registry);
            family_ptr = &family;
            m_counter_families.insert({name, family_ptr});
        } else {
            family_ptr = it->second;
        }

        std::map< std::string, std::string > label_pairs;
        if (!label_pair.first.empty() && !label_pair.second.empty()) {
            label_pairs = {{"entity", instance_name}, {label_pair.first, label_pair.second}};
        } else {
            label_pairs = {{"entity", instance_name}};
        }

        return std::make_shared< PrometheusReportCounter >(*family_ptr, label_pairs);
    }

    std::shared_ptr< ReportGauge > add_gauge(const std::string& name, const std::string& desc,
                                             const std::string& instance_name,
                                             const metric_label& label_pair = {"", ""}) {
        prometheus::Family< prometheus::Gauge >* family_ptr;

        std::unique_lock lk(m_mutex);
        auto it = m_gauge_families.find(name);
        if (it == m_gauge_families.end()) {
            auto& family = prometheus::BuildGauge().Name(name).Help(desc).Register(*m_registry);
            family_ptr = &family;
            m_gauge_families.insert({name, family_ptr});
        } else {
            family_ptr = it->second;
        }

        std::map< std::string, std::string > label_pairs;
        if (!label_pair.first.empty() && !label_pair.second.empty()) {
            label_pairs = {{"entity", instance_name}, {label_pair.first, label_pair.second}};
        } else {
            label_pairs = {{"entity", instance_name}};
        }

        return std::make_shared< PrometheusReportGauge >(*family_ptr, label_pairs);
    }

    std::shared_ptr< ReportHistogram > add_histogram(const std::string& name, const std::string& desc,
                                                     const std::string& instance_name,
                                                     const hist_bucket_boundaries_t& bkt_boundaries,
                                                     const metric_label& label_pair = {"", ""}) {
        prometheus::Family< prometheus::Histogram >* family_ptr = nullptr;

        std::unique_lock lk(m_mutex);
        auto it = m_histogram_families.find(name);
        if (it == m_histogram_families.end()) {
            auto& family = prometheus::BuildHistogram().Name(name).Help(desc).Register(*m_registry);
            family_ptr = &family;
            m_histogram_families.insert({name, family_ptr});
        } else {
            family_ptr = it->second;
        }

        std::map< std::string, std::string > label_pairs;
        if (!label_pair.first.empty() && !label_pair.second.empty()) {
            label_pairs = {{"entity", instance_name}, {label_pair.first, label_pair.second}};
        } else {
            label_pairs = {{"entity", instance_name}};
        }

        return std::make_shared< PrometheusReportHistogram >(*family_ptr, label_pairs, bkt_boundaries);
    }

    void remove_counter(const std::string& name, const std::shared_ptr< ReportCounter >& rc) override {
        std::unique_lock lk(m_mutex);
        auto it = m_counter_families.find(name);
        if (it == m_counter_families.end()) {
            LOGERROR("Unable to locate the counter of name {} to remove", name);
            return;
        }

        auto family_ptr = it->second;
        auto prc = std::static_pointer_cast< PrometheusReportCounter >(rc);
        family_ptr->Remove(&prc->m_counter);
    }

    virtual void remove_gauge(const std::string& name, const std::shared_ptr< ReportGauge >& rg) {
        std::unique_lock lk(m_mutex);
        auto it = m_gauge_families.find(name);
        if (it == m_gauge_families.end()) {
            LOGERROR("Unable to locate the gauge of name {} to remove", name);
            return;
        }

        auto family_ptr = it->second;
        auto prg = std::static_pointer_cast< PrometheusReportGauge >(rg);
        family_ptr->Remove(&prg->m_gauge);
    }

    virtual void remove_histogram(const std::string& name, const std::shared_ptr< ReportHistogram >& rh) {
        std::unique_lock lk(m_mutex);
        auto it = m_histogram_families.find(name);
        if (it == m_histogram_families.end()) {
            LOGERROR("Unable to locate the histogram of name {} to remove", name);
            return;
        }

        auto family_ptr = it->second;
        auto prh = std::static_pointer_cast< PrometheusReportHistogram >(rh);
        family_ptr->Remove(&prh->m_histogram);
    }

    std::string serialize(ReportFormat format) {
        if (format != m_cur_serializer_format) {
            // If user wants different formatter now, change the serializer
            switch (format) {
            case ReportFormat::kTextFormat:
                m_serializer.reset(new prometheus::TextSerializer());
                break;
            default:
                assert(0);
                format = ReportFormat::kTextFormat;
                m_serializer.reset(new prometheus::TextSerializer());
            }

            m_cur_serializer_format = format;
        }
        return m_serializer->Serialize(m_registry->Collect());
    }

private:
    std::mutex m_mutex;
    std::shared_ptr< prometheus::Registry > m_registry;
    std::unordered_map< std::string, prometheus::Family< prometheus::Counter >* > m_counter_families;
    std::unordered_map< std::string, prometheus::Family< prometheus::Gauge >* > m_gauge_families;
    std::unordered_map< std::string, prometheus::Family< prometheus::Histogram >* > m_histogram_families;

    std::unique_ptr< prometheus::Serializer > m_serializer;
    ReportFormat m_cur_serializer_format;
};
} // namespace sisl
