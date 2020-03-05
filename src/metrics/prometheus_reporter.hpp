//
// Created by Kadayam, Hari on 2/5/19.
//

#ifndef SISL_METRICS_PROMETHEUS_REPORTER_HPP
#define SISL_METRICS_PROMETHEUS_REPORTER_HPP

#include <map>
#include <unordered_map>
#include <memory>
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

namespace sisl {

class PrometheusReportCounter : public ReportCounter {
public:
    PrometheusReportCounter(prometheus::Family< prometheus::Counter >&  family,
                            const std::map< std::string, std::string >& label_pairs) :
            m_counter(family.Add(label_pairs)) {}

    virtual void set_value(double value) override {
        double counter_value = m_counter.Value();
        double diff = value - counter_value;
        assert(diff >= 0);

        // we rely on prometheus::counter to check whether the passed value is < 0, and if so, discard the passed value
        m_counter.Increment(diff);
    };

private:
    prometheus::Counter& m_counter;
};

class PrometheusReportGauge : public ReportGauge {
public:
    PrometheusReportGauge(prometheus::Family< prometheus::Gauge >&    family,
                          const std::map< std::string, std::string >& label_pairs) :
            m_gauge(family.Add(label_pairs)) {}

    virtual void set_value(double value) override { m_gauge.Set(value); };

private:
    prometheus::Gauge& m_gauge;
};

class PrometheusReportHistogram : public ReportHistogram {
public:
    PrometheusReportHistogram(prometheus::Family< prometheus::Histogram >& family,
                              const std::map< std::string, std::string >&  label_pairs,
                              const hist_bucket_boundaries_t&              bkt_boundaries) :
            m_histogram(family.Add(label_pairs, bkt_boundaries)) {}

    virtual void set_value(const std::vector< double >& bucket_values, double sum) {
        // Use modified prometheus method (not part of original repo)
        m_histogram.TransferBucketCounters(bucket_values, sum);
    }

private:
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
                                                 const std::string&  instance_name,
                                                 const metric_label& label_pair = {"", ""}) {
        static uint64_t                                                      __unique_id = 0;
        std::shared_ptr< PrometheusReportCounter >                           ret;
        std::pair< std::string, prometheus::Family< prometheus::Counter >* > family_pair;

        std::unique_lock lk(m_mutex);
        auto             it = m_counter_families.find(name);
        if (it == m_counter_families.end()) {
            auto& family = prometheus::BuildCounter().Name(name).Help(desc).Register(*m_registry);
            family_pair = std::make_pair<>(name, &family);
            m_counter_families.insert(family_pair);
        } else {
            family_pair = *it;
        }

        // If 2 instances are provided with same name (unknowingly), prometheus with same label pairs, return the same
        // prometheus::Counter pointer, which means if one of them freed, other could access it. To prevent that, we
        // are creating a unique name to each PrometheusReport.... so that we have one per registration.
        std::string unique_name("Counter");
        unique_name += std::to_string(++__unique_id);

        std::map< std::string, std::string > label_pairs;
        if (!label_pair.first.empty() && !label_pair.second.empty()) {
            label_pairs = {
                {"instance", instance_name}, {"unique_id", unique_name}, {label_pair.first, label_pair.second}};
        } else {
            label_pairs = {{"instance", instance_name}, {"unique_id", unique_name}};
        }

        return std::make_shared< PrometheusReportCounter >(*family_pair.second, label_pairs);
    }

    std::shared_ptr< ReportGauge > add_gauge(const std::string& name, const std::string& desc,
                                             const std::string&  instance_name,
                                             const metric_label& label_pair = {"", ""}) {
        static uint64_t                                                    __unique_id = 0;
        std::shared_ptr< ReportGauge >                                     ret;
        std::pair< std::string, prometheus::Family< prometheus::Gauge >* > family_pair;

        std::unique_lock lk(m_mutex);
        auto             it = m_gauge_families.find(name);
        if (it == m_gauge_families.end()) {
            auto& family = prometheus::BuildGauge().Name(name).Help(desc).Register(*m_registry);
            family_pair = std::make_pair<>(name, &family);
            m_gauge_families.insert(family_pair);
        } else {
            family_pair = *it;
        }

        std::string unique_name("Gauge");
        unique_name += std::to_string(++__unique_id);

        std::map< std::string, std::string > label_pairs;
        if (!label_pair.first.empty() && !label_pair.second.empty()) {
            label_pairs = {
                {"instance", instance_name}, {"unique_id", unique_name}, {label_pair.first, label_pair.second}};
        } else {
            label_pairs = {{"instance", instance_name}, {"unique_id", unique_name}};
        }

        return std::make_shared< PrometheusReportGauge >(*family_pair.second, label_pairs);
    }

    std::shared_ptr< ReportHistogram > add_histogram(const std::string& name, const std::string& desc,
                                                     const std::string&              instance_name,
                                                     const hist_bucket_boundaries_t& bkt_boundaries,
                                                     const metric_label&             label_pair = {"", ""}) {
        static uint64_t                                                        __unique_id = 0;
        std::shared_ptr< ReportHistogram >                                     ret;
        std::pair< std::string, prometheus::Family< prometheus::Histogram >* > family_pair;

        std::unique_lock lk(m_mutex);
        auto             it = m_histogram_families.find(name);
        if (it == m_histogram_families.end()) {
            auto& family = prometheus::BuildHistogram().Name(name).Help(desc).Register(*m_registry);
            family_pair = std::make_pair<>(name, &family);
            m_histogram_families.insert(family_pair);
        } else {
            family_pair = *it;
        }

        std::string unique_name("Histogram");
        unique_name += std::to_string(++__unique_id);

        std::map< std::string, std::string > label_pairs;
        if (!label_pair.first.empty() && !label_pair.second.empty()) {
            label_pairs = {
                {"instance", instance_name}, {"unique_id", unique_name}, {label_pair.first, label_pair.second}};
        } else {
            label_pairs = {{"instance", instance_name}, {"unique_id", unique_name}};
        }

        return std::make_shared< PrometheusReportHistogram >(*family_pair.second, label_pairs, bkt_boundaries);
    }

    std::string serialize(ReportFormat format) {
        if (format != m_cur_serializer_format) {
            // If user wants different formatter now, change the serializer
            switch (format) {
            case ReportFormat::kTextFormat: m_serializer.reset(new prometheus::TextSerializer()); break;
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
    std::mutex                                                                      m_mutex;
    std::shared_ptr< prometheus::Registry >                                         m_registry;
    std::unordered_map< std::string, prometheus::Family< prometheus::Counter >* >   m_counter_families;
    std::unordered_map< std::string, prometheus::Family< prometheus::Gauge >* >     m_gauge_families;
    std::unordered_map< std::string, prometheus::Family< prometheus::Histogram >* > m_histogram_families;

    std::unique_ptr< prometheus::Serializer > m_serializer;
    ReportFormat                              m_cur_serializer_format;
};
} // namespace sisl
#endif // ASYNC_HTTP_PROMETHEUS_REPORTER_HPP
