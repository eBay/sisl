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

#include <sisl/logging/logging.h>

namespace sisl {

class PrometheusReportCounter : public ReportCounter {
public:
    PrometheusReportCounter(prometheus::Family< prometheus::Counter >& family,
                            const std::map< std::string, std::string >& label_pairs);
    void set_value(double value) override;

    prometheus::Counter& m_counter;
};

class PrometheusReportGauge : public ReportGauge {
public:
    PrometheusReportGauge(prometheus::Family< prometheus::Gauge >& family,
                          const std::map< std::string, std::string >& label_pairs);
    void set_value(double value) override;

    prometheus::Gauge& m_gauge;
};

class PrometheusReportHistogram : public ReportHistogram {
public:
    PrometheusReportHistogram(prometheus::Family< prometheus::Histogram >& family,
                              const std::map< std::string, std::string >& label_pairs,
                              const hist_bucket_boundaries_t& bkt_boundaries);
    void set_value(std::vector< double >& bucket_values, double sum) override;

    prometheus::Histogram& m_histogram;
    const hist_bucket_boundaries_t& m_bkt_boundaries;
};

class PrometheusReportSumCount : public ReportSumCount {
public:
    PrometheusReportSumCount(prometheus::Family< prometheus::Counter >& sum_family,
                             prometheus::Family< prometheus::Counter >& count_family,
                             const std::map< std::string, std::string >& label_pairs);
    void set_value(int64_t count, double sum) override;

    prometheus::Counter& m_sum;
    prometheus::Counter& m_count;
};

class PrometheusReporter : public Reporter {
public:
    PrometheusReporter();
    ~PrometheusReporter() override = default;

    std::shared_ptr< ReportCounter > add_counter(const std::string& name, const std::string& desc,
                                                 const std::string& instance_name,
                                                 const metric_label& label_pair = {"", ""}) override;

    std::shared_ptr< ReportGauge > add_gauge(const std::string& name, const std::string& desc,
                                             const std::string& instance_name,
                                             const metric_label& label_pair = {"", ""}) override;

    std::shared_ptr< ReportHistogram > add_histogram(const std::string& name, const std::string& desc,
                                                     const std::string& instance_name,
                                                     const hist_bucket_boundaries_t& bkt_boundaries,
                                                     const metric_label& label_pair = {"", ""}) override;

    std::shared_ptr< ReportSumCount > add_sum_count(const std::string& name, const std::string& desc,
                                                    const std::string& instance_name,
                                                    const metric_label& label_pair = {"", ""}) override;

    void remove_counter(const std::string& name, const std::shared_ptr< ReportCounter >& rc) override;
    void remove_gauge(const std::string& name, const std::shared_ptr< ReportGauge >& rg) override;
    void remove_histogram(const std::string& name, const std::shared_ptr< ReportHistogram >& rh) override;
    void remove_sum_count(const std::string& name, const std::shared_ptr< ReportSumCount >& rsc) override;

    std::string serialize(ReportFormat format) override;

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
