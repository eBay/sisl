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
#include <sisl/metrics/prometheus_reporter.hpp>

namespace sisl {

// ----- PrometheusReportCounter -----

PrometheusReportCounter::PrometheusReportCounter(prometheus::Family< prometheus::Counter >& family,
                                                 const std::map< std::string, std::string >& label_pairs) :
        m_counter(family.Add(label_pairs)) {}

void PrometheusReportCounter::set_value(double value) {
    double diff = value - m_counter.Value();
    assert(diff >= 0);
    m_counter.Increment(diff);
}

// ----- PrometheusReportGauge -----

PrometheusReportGauge::PrometheusReportGauge(prometheus::Family< prometheus::Gauge >& family,
                                             const std::map< std::string, std::string >& label_pairs) :
        m_gauge(family.Add(label_pairs)) {}

void PrometheusReportGauge::set_value(double value) { m_gauge.Set(value); }

// ----- PrometheusReportHistogram -----

PrometheusReportHistogram::PrometheusReportHistogram(prometheus::Family< prometheus::Histogram >& family,
                                                     const std::map< std::string, std::string >& label_pairs,
                                                     const hist_bucket_boundaries_t& bkt_boundaries) :
        m_histogram(family.Add(label_pairs, bkt_boundaries)), m_bkt_boundaries{bkt_boundaries} {}

void PrometheusReportHistogram::set_value(std::vector< double >& bucket_values, double sum) {
    bucket_values.resize(m_bkt_boundaries.size() + 1);
    m_histogram.~Histogram();
    auto* inplace_hist =
        new (static_cast< void* >(std::addressof(m_histogram))) prometheus::Histogram(m_bkt_boundaries);
    inplace_hist->ObserveMultiple(bucket_values, sum);
}

// ----- PrometheusReportSumCount -----

PrometheusReportSumCount::PrometheusReportSumCount(prometheus::Family< prometheus::Counter >& sum_family,
                                                   prometheus::Family< prometheus::Counter >& count_family,
                                                   const std::map< std::string, std::string >& label_pairs) :
        m_sum(sum_family.Add(label_pairs)), m_count(count_family.Add(label_pairs)) {}

void PrometheusReportSumCount::set_value(int64_t count, double sum) {
    m_sum.Increment(sum - m_sum.Value());
    m_count.Increment(count - m_count.Value());
}

// ----- PrometheusReporter -----

PrometheusReporter::PrometheusReporter() {
    m_registry = std::make_shared< prometheus::Registry >();
    m_serializer = std::make_unique< prometheus::TextSerializer >();
    m_cur_serializer_format = TEXT_FORMAT;
}

std::shared_ptr< ReportCounter > PrometheusReporter::add_counter(const std::string& name, const std::string& desc,
                                                                 const std::string& instance_name,
                                                                 const metric_label& label_pair) {
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

std::shared_ptr< ReportGauge > PrometheusReporter::add_gauge(const std::string& name, const std::string& desc,
                                                             const std::string& instance_name,
                                                             const metric_label& label_pair) {
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

std::shared_ptr< ReportHistogram > PrometheusReporter::add_histogram(const std::string& name, const std::string& desc,
                                                                     const std::string& instance_name,
                                                                     const hist_bucket_boundaries_t& bkt_boundaries,
                                                                     const metric_label& label_pair) {
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

std::shared_ptr< ReportSumCount > PrometheusReporter::add_sum_count(const std::string& name, const std::string& desc,
                                                                    const std::string& instance_name,
                                                                    const metric_label& label_pair) {
    std::unique_lock lk(m_mutex);

    auto sum_name = name + "_sum";
    auto sum_it = m_counter_families.find(sum_name);
    prometheus::Family< prometheus::Counter >* sum_family;
    if (sum_it == m_counter_families.end()) {
        auto& family = prometheus::BuildCounter().Name(sum_name).Help(desc + " (sum)").Register(*m_registry);
        sum_family = &family;
        m_counter_families[sum_name] = sum_family;
    } else {
        sum_family = sum_it->second;
    }

    auto count_name = name + "_count";
    auto count_it = m_counter_families.find(count_name);
    prometheus::Family< prometheus::Counter >* count_family;
    if (count_it == m_counter_families.end()) {
        auto& family = prometheus::BuildCounter().Name(count_name).Help(desc + " (count)").Register(*m_registry);
        count_family = &family;
        m_counter_families[count_name] = count_family;
    } else {
        count_family = count_it->second;
    }

    std::map< std::string, std::string > label_pairs;
    if (!label_pair.first.empty() && !label_pair.second.empty()) {
        label_pairs = {{"entity", instance_name}, {label_pair.first, label_pair.second}};
    } else {
        label_pairs = {{"entity", instance_name}};
    }
    return std::make_shared< PrometheusReportSumCount >(*sum_family, *count_family, label_pairs);
}

void PrometheusReporter::remove_counter(const std::string& name, const std::shared_ptr< ReportCounter >& rc) {
    std::unique_lock lk(m_mutex);
    auto it = m_counter_families.find(name);
    if (it == m_counter_families.end()) {
        LOGERROR("Unable to locate the counter of name {} to remove", name);
        return;
    }
    it->second->Remove(&std::static_pointer_cast< PrometheusReportCounter >(rc)->m_counter);
}

void PrometheusReporter::remove_gauge(const std::string& name, const std::shared_ptr< ReportGauge >& rg) {
    std::unique_lock lk(m_mutex);
    auto it = m_gauge_families.find(name);
    if (it == m_gauge_families.end()) {
        LOGERROR("Unable to locate the gauge of name {} to remove", name);
        return;
    }
    it->second->Remove(&std::static_pointer_cast< PrometheusReportGauge >(rg)->m_gauge);
}

void PrometheusReporter::remove_histogram(const std::string& name, const std::shared_ptr< ReportHistogram >& rh) {
    std::unique_lock lk(m_mutex);
    auto it = m_histogram_families.find(name);
    if (it == m_histogram_families.end()) {
        LOGERROR("Unable to locate the histogram of name {} to remove", name);
        return;
    }
    it->second->Remove(&std::static_pointer_cast< PrometheusReportHistogram >(rh)->m_histogram);
}

void PrometheusReporter::remove_sum_count(const std::string& name, const std::shared_ptr< ReportSumCount >& rsc) {
    std::unique_lock lk(m_mutex);

    auto sum_name = name + "_sum";
    auto sum_it = m_counter_families.find(sum_name);
    if (sum_it != m_counter_families.end()) {
        sum_it->second->Remove(&std::static_pointer_cast< PrometheusReportSumCount >(rsc)->m_sum);
    } else {
        LOGERROR("Unable to locate the sum counter of name {} to remove", sum_name);
    }

    auto count_name = name + "_count";
    auto count_it = m_counter_families.find(count_name);
    if (count_it != m_counter_families.end()) {
        count_it->second->Remove(&std::static_pointer_cast< PrometheusReportSumCount >(rsc)->m_count);
    } else {
        LOGERROR("Unable to locate the count counter of name {} to remove", count_name);
    }
}

std::string PrometheusReporter::serialize(ReportFormat format) {
    if (format != m_cur_serializer_format) {
        switch (format) {
        case ReportFormat::TEXT_FORMAT:
            m_serializer = std::make_unique< prometheus::TextSerializer >();
            break;
        default:
            assert(0);
            format = ReportFormat::TEXT_FORMAT;
            m_serializer = std::make_unique< prometheus::TextSerializer >();
        }
        m_cur_serializer_format = format;
    }
    return m_serializer->Serialize(m_registry->Collect());
}

} // namespace sisl
