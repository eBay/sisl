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
 * under the License is distributed on  * an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#pragma once

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
    virtual std::shared_ptr< ReportCounter > add_counter(const std::string& name, const std::string& desc,
                                                         const std::string& instance_name,
                                                         const metric_label& label_pair = {"", ""}) = 0;
    virtual std::shared_ptr< ReportGauge > add_gauge(const std::string& name, const std::string& desc,
                                                     const std::string& instance_name,
                                                     const metric_label& label_pair = {"", ""}) = 0;
    virtual std::shared_ptr< ReportHistogram > add_histogram(const std::string& name, const std::string& desc,
                                                             const std::string& instance_name,
                                                             const hist_bucket_boundaries_t& bkt_boundaries,
                                                             const metric_label& label_pair = {"", ""}) = 0;

    virtual void remove_counter(const std::string& name, const std::shared_ptr< ReportCounter >& hist) = 0;
    virtual void remove_gauge(const std::string& name, const std::shared_ptr< ReportGauge >& hist) = 0;
    virtual void remove_histogram(const std::string& name, const std::shared_ptr< ReportHistogram >& hist) = 0;

    virtual std::string serialize(ReportFormat format) = 0;
};
} // namespace sisl
