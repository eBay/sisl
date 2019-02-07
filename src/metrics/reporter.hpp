//
// Created by Kadayam, Hari on 2/5/19.
//

#ifndef SISL_METRICS_REPORTER_HPP
#define SISL_METRICS_REPORTER_HPP

#include <map>
#include <string>
#include <memory>
#include "histogram_buckets.hpp"

namespace sisl {
typedef std::pair< std::string, std::string > metric_label;

enum ReportFormat { kUnknownFormat, kTextFormat, kJsonFormat, kProtoBufferFormat };

class ReportCounter {
public:
    virtual void set_value(double value) = 0;
};

class ReportGauge {
public:
    virtual void set_value(double value) = 0;
};

class ReportHistogram {
public:
    virtual void set_value(const std::vector< double >& bucket_values, double sum) = 0;
};

class Reporter {
public:
    virtual ~Reporter() = default;
    virtual std::shared_ptr< ReportCounter >   add_counter(const std::string& name, const std::string& desc,
                                                           const std::string&  instance_name,
                                                           const metric_label& label_pair = {"", ""}) = 0;
    virtual std::shared_ptr< ReportGauge >     add_gauge(const std::string& name, const std::string& desc,
                                                         const std::string&  instance_name,
                                                         const metric_label& label_pair = {"", ""}) = 0;
    virtual std::shared_ptr< ReportHistogram > add_histogram(const std::string& name, const std::string& desc,
                                                             const std::string& instance_name,
                                                             const hist_bucket_boundaries_t& bkt_boundaries,
                                                             const metric_label&  label_pair = {"", ""}) = 0;
    virtual std::string                        serialize(ReportFormat format) = 0;
};
} // namespace sisl
#endif // ASYNC_HTTP_REPORTER_HPP
