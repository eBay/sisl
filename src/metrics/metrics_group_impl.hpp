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

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "histogram_buckets.hpp"
#include "prometheus_reporter.hpp"
#include "../utility/thread_buffer.hpp"

namespace sisl {
using on_gather_cb_t = std::function< void(void) >;

enum class group_impl_type_t : uint8_t {
    rcu,
    thread_buf_volatile,
    thread_buf_signal,
    atomic,
};

enum class _publish_as : uint8_t {
    publish_as_counter,
    publish_as_gauge,
    publish_as_histogram,
};

/****************************** Counter ************************************/
class CounterValue {
public:
    friend class AtomicCounterValue;

    CounterValue() = default;
    CounterValue(const CounterValue&) = default;
    CounterValue(CounterValue&&) noexcept = default;
    CounterValue& operator=(const CounterValue&) = delete;
    CounterValue& operator=(CounterValue&&) noexcept = delete;

    void increment(const int64_t value = 1) { m_value += value; }
    void decrement(const int64_t value = 1) { m_value -= value; }
    int64_t get() const { return m_value; }
    int64_t merge(const CounterValue& other) {
        this->m_value += other.m_value;
        return this->m_value;
    }

private:
    int64_t m_value{0};
};

class CounterStaticInfo {
    friend class CounterDynamicInfo;

public:
    CounterStaticInfo(const std::string& name, const std::string& desc, const std::string& report_name = "",
                      const metric_label& label_pair = {"", ""});

    CounterStaticInfo(const CounterStaticInfo&) = default;
    CounterStaticInfo(CounterStaticInfo&&) noexcept = delete;
    CounterStaticInfo& operator=(const CounterStaticInfo&) = delete;
    CounterStaticInfo& operator=(CounterStaticInfo&&) noexcept = delete;

    [[nodiscard]] const std::string& name() const { return m_name; }
    [[nodiscard]] const std::string& desc() const { return m_desc; }
    [[nodiscard]] std::string label_pair() const {
        return (!m_label_pair.first.empty() && !m_label_pair.second.empty())
            ? m_label_pair.first + "-" + m_label_pair.second
            : "";
    }

private:
    std::string m_name;
    std::string m_desc;
    std::pair< std::string, std::string > m_label_pair;
};

class CounterDynamicInfo {
public:
    CounterDynamicInfo(const CounterStaticInfo& static_info, const std::string& instance_name,
                       _publish_as ptype = _publish_as::publish_as_counter);

    CounterDynamicInfo(const CounterDynamicInfo&) = default;
    CounterDynamicInfo(CounterDynamicInfo&&) noexcept = delete;
    CounterDynamicInfo& operator=(const CounterDynamicInfo&) = delete;
    CounterDynamicInfo& operator=(CounterDynamicInfo&&) noexcept = delete;

    void publish(const CounterValue& value);
    void unregister(const CounterStaticInfo& static_info);

private:
    std::shared_ptr< ReportCounter >& as_counter() {
        return std::get< std::shared_ptr< ReportCounter > >(m_report_counter_gauge);
    }

    std::shared_ptr< ReportGauge >& as_gauge() {
        return std::get< std::shared_ptr< ReportGauge > >(m_report_counter_gauge);
    }

    [[nodiscard]] bool is_counter_reporter() const {
        return std::holds_alternative< std::shared_ptr< ReportCounter > >(m_report_counter_gauge);
    }

private:
    std::variant< std::shared_ptr< ReportCounter >, std::shared_ptr< ReportGauge > > m_report_counter_gauge;
};

/****************************** Gauge ************************************/
class GaugeValue {
public:
    GaugeValue() : m_value{0} {}
    GaugeValue(const std::atomic< int64_t >& oval) : m_value{oval.load(std::memory_order_relaxed)} {}
    GaugeValue(const GaugeValue& other) : m_value{other.get()} {}
    GaugeValue& operator=(const GaugeValue& rhsOther) {
        m_value.store(rhsOther.get(), std::memory_order_relaxed);
        return *this;
    }
    // use copy operations for move since std::atomic is not movable
    GaugeValue(GaugeValue&& other) noexcept : m_value{other.get()} {}
    GaugeValue& operator=(GaugeValue&& rhsOther) noexcept {
        m_value.store(rhsOther.get(), std::memory_order_relaxed);
        return *this;
    }

    inline void update(const int64_t value) { m_value.store(value, std::memory_order_relaxed); }
    [[nodiscard]] int64_t get() const { return m_value.load(std::memory_order_relaxed); }

private:
    std::atomic< int64_t > m_value;
};

class GaugeStaticInfo {
    friend class MetricsGroup;
    friend class GaugeDynamicInfo;

public:
    GaugeStaticInfo(const std::string& name, const std::string& desc, const std::string& report_name = "",
                    const metric_label& label_pair = {"", ""});

    GaugeStaticInfo(const GaugeStaticInfo&) = default;
    GaugeStaticInfo(GaugeStaticInfo&&) noexcept = delete;
    GaugeStaticInfo& operator=(const GaugeStaticInfo&) = delete;
    GaugeStaticInfo& operator=(GaugeStaticInfo&&) noexcept = delete;

    [[nodiscard]] const std::string& name() const { return m_name; }
    [[nodiscard]] const std::string& desc() const { return m_desc; }
    [[nodiscard]] std::string label_pair() const {
        return (!m_label_pair.first.empty() && !m_label_pair.second.empty())
            ? m_label_pair.first + "-" + m_label_pair.second
            : "";
    }

private:
    const std::string m_name;
    const std::string m_desc;
    std::pair< std::string, std::string > m_label_pair;
};

class GaugeDynamicInfo {
public:
    GaugeDynamicInfo(const GaugeStaticInfo& static_info, const std::string& instance_name);

    GaugeDynamicInfo(const GaugeDynamicInfo&) = default;
    GaugeDynamicInfo(GaugeDynamicInfo&&) noexcept = delete;
    GaugeDynamicInfo& operator=(const GaugeDynamicInfo&) = delete;
    GaugeDynamicInfo& operator=(GaugeDynamicInfo&&) noexcept = delete;

    void publish(const GaugeValue& value);
    void unregister(const GaugeStaticInfo& static_info);

private:
    std::shared_ptr< ReportGauge > m_report_gauge;
};

/****************************** Histogram ************************************/
class HistogramValue {
public:
    friend class AtomicHistogramValue;

    HistogramValue() = default;
    HistogramValue(const HistogramValue&) = default;
    HistogramValue(HistogramValue&&) noexcept = default;
    HistogramValue& operator=(const HistogramValue&) = delete;
    HistogramValue& operator=(HistogramValue&&) noexcept = delete;

    void observe(const int64_t value, const hist_bucket_boundaries_t& boundaries, const uint64_t count = 1) {
        const auto lower{std::lower_bound(std::cbegin(boundaries), std::cend(boundaries), value)};
        const auto bkt_idx{std::distance(std::cbegin(boundaries), lower)};
        m_freqs[bkt_idx] += count;
        m_sum += (value * count);
    }

    void merge(const HistogramValue& other, const hist_bucket_boundaries_t& boundaries) {
        for (size_t i{0}; i < boundaries.size(); ++i) {
            this->m_freqs[i] += other.m_freqs[i];
        }
        this->m_sum += other.m_sum;
    }
    [[nodiscard]] const auto& get_freqs() const { return m_freqs; }
    [[nodiscard]] int64_t get_sum() const { return m_sum; }

private:
    std::array< int64_t, HistogramBuckets::max_hist_bkts > m_freqs{};
    int64_t m_sum{0};
};

static_assert(std::is_trivially_copyable< HistogramValue >::value, "Expecting HistogramValue to be trivally copyable");

class HistogramStaticInfo {
    friend class HistogramDynamicInfo;

public:
    HistogramStaticInfo(const std::string& name, const std::string& desc, const std::string& report_name = "",
                        const metric_label& label_pair = {"", ""},
                        const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets));

    HistogramStaticInfo(const HistogramStaticInfo&) = default;
    HistogramStaticInfo(HistogramStaticInfo&&) noexcept = delete;
    HistogramStaticInfo& operator=(const HistogramStaticInfo&) = delete;
    HistogramStaticInfo& operator=(HistogramStaticInfo&&) noexcept = delete;

    [[nodiscard]] const std::string& name() const { return m_name; }
    [[nodiscard]] const std::string& desc() const { return m_desc; }
    [[nodiscard]] std::string label_pair() const {
        return (!m_label_pair.first.empty() && !m_label_pair.second.empty())
            ? m_label_pair.first + "-" + m_label_pair.second
            : "";
    }

    [[nodiscard]] const hist_bucket_boundaries_t& get_boundaries() const { return m_bkt_boundaries; }

private:
    const std::string m_name;
    const std::string m_desc;
    std::pair< std::string, std::string > m_label_pair;
    const hist_bucket_boundaries_t& m_bkt_boundaries;
};

class HistogramDynamicInfo {
    friend class MetricsGroupImpl;

public:
    HistogramDynamicInfo(const HistogramStaticInfo& static_info, const std::string& instance_name,
                         _publish_as ptype = _publish_as::publish_as_histogram);

    HistogramDynamicInfo(const HistogramDynamicInfo&) = default;
    HistogramDynamicInfo(HistogramStaticInfo&&) noexcept = delete;
    HistogramDynamicInfo& operator=(const HistogramDynamicInfo&) = delete;
    HistogramDynamicInfo& operator=(HistogramDynamicInfo&&) noexcept = delete;

    [[nodiscard]] double percentile(const HistogramValue& hvalue, const hist_bucket_boundaries_t&,
                                    const float pcntl) const;
    [[nodiscard]] int64_t count(const HistogramValue& hvalue) const;
    [[nodiscard]] double average(const HistogramValue& hvalue) const;

    void publish(const HistogramValue& hvalue);
    void unregister(const HistogramStaticInfo& static_info);

private:
    std::shared_ptr< ReportHistogram >& as_histogram() {
        return std::get< std::shared_ptr< ReportHistogram > >(m_report_histogram_gauge);
    }

    std::shared_ptr< ReportGauge >& as_gauge() {
        return std::get< std::shared_ptr< ReportGauge > >(m_report_histogram_gauge);
    }

    [[nodiscard]] bool is_histogram_reporter() const {
        return std::holds_alternative< std::shared_ptr< ReportHistogram > >(m_report_histogram_gauge);
    }

private:
    std::variant< std::shared_ptr< ReportHistogram >, std::shared_ptr< ReportGauge > > m_report_histogram_gauge;
};

class MetricsGroupImpl;
using MetricsGroupImplPtr = std::shared_ptr< MetricsGroupImpl >;

// This is one class per type and not one per instance
class MetricsGroupStaticInfo {
    friend class MetricsGroupImpl;
    friend class MetricsGroup;

public:
    MetricsGroupStaticInfo() = default;
    MetricsGroupStaticInfo(const MetricsGroupStaticInfo&) = delete;
    MetricsGroupStaticInfo(MetricsGroupStaticInfo&&) noexcept = delete;
    MetricsGroupStaticInfo& operator=(const MetricsGroupStaticInfo&) = delete;
    MetricsGroupStaticInfo& operator=(MetricsGroupStaticInfo&&) noexcept = delete;

    static std::shared_ptr< MetricsGroupStaticInfo > create_or_get_info(const std::string& grp_name);

    MetricsGroupStaticInfo(const std::string& grp_name);
    uint64_t register_counter(const std::string& name, const std::string& desc, const std::string& report_name = "",
                              const metric_label& label_pair = {"", ""});

    uint64_t register_gauge(const std::string& name, const std::string& desc, const std::string& report_name = "",
                            const metric_label& label_pair = {"", ""});

    uint64_t register_histogram(const std::string& name, const std::string& desc, const std::string& report_name = "",
                                const metric_label& label_pair = {"", ""},
                                const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets));

    [[nodiscard]] const CounterStaticInfo& get_counter_info(uint64_t index) const;
    [[nodiscard]] const GaugeStaticInfo& get_gauge_info(uint64_t index) const;
    [[nodiscard]] const HistogramStaticInfo& get_histogram_info(uint64_t index) const;

public:
    std::string m_grp_name;
    std::mutex m_mutex;

    std::vector< CounterStaticInfo > m_counters;
    std::vector< GaugeStaticInfo > m_gauges;
    std::vector< HistogramStaticInfo > m_histograms;
    bool m_reg_pending{true};
};

using counter_gather_cb_t = std::function< void(uint64_t, const CounterValue&) >;
using gauge_gather_cb_t = std::function< void(uint64_t, const GaugeValue&) >;
using histogram_gather_cb_t = std::function< void(uint64_t, const HistogramValue&) >;

class MetricsGroupImpl {
private:
    [[nodiscard]] auto lock() { return std::lock_guard< decltype(m_mutex) >(m_mutex); }

public:
    MetricsGroupImpl(const std::string& grp_name, const std::string& inst_name);
    ~MetricsGroupImpl();

    MetricsGroupImpl(const MetricsGroupImpl&) = delete;
    MetricsGroupImpl(MetricsGroupImpl&&) noexcept = delete;
    MetricsGroupImpl& operator=(const MetricsGroupImpl&) = delete;
    MetricsGroupImpl& operator=(MetricsGroupImpl&&) noexcept = delete;

    void registration_completed();

    /* Counter */
    uint64_t register_counter(const std::string& name, const std::string& desc, const std::string& report_name = "",
                              const metric_label& label_pair = {"", ""},
                              _publish_as ptype = _publish_as::publish_as_counter);
    uint64_t register_counter(const std::string& name, const std::string& desc, const metric_label& label_pair,
                              _publish_as ptype = _publish_as::publish_as_counter);
    uint64_t register_counter(const std::string& name, const std::string& desc, _publish_as ptype);

    uint64_t register_gauge(const std::string& name, const std::string& desc, const std::string& report_name = "",
                            const metric_label& label_pair = {"", ""});
    uint64_t register_gauge(const std::string& name, const std::string& desc, const metric_label& label_pair);

    uint64_t register_histogram(const std::string& name, const std::string& desc, const std::string& report_name = "",
                                const metric_label& label_pair = {"", ""},
                                const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets),
                                _publish_as ptype = _publish_as::publish_as_histogram);
    uint64_t register_histogram(const std::string& name, const std::string& desc, const metric_label& label_pair,
                                const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets),
                                _publish_as ptype = _publish_as::publish_as_histogram);
    uint64_t register_histogram(const std::string& name, const std::string& desc,
                                const hist_bucket_boundaries_t& bkt_boundaries,
                                _publish_as ptype = _publish_as::publish_as_histogram);
    uint64_t register_histogram(const std::string& name, const std::string& desc, _publish_as ptype);

    virtual void counter_increment(const uint64_t index, const int64_t val = 1) = 0;
    virtual void counter_decrement(const uint64_t index, const int64_t val = 1) = 0;

    void gauge_update(const uint64_t index, const int64_t val);

    virtual void histogram_observe(const uint64_t index, const int64_t val, const uint64_t count) = 0;
    virtual void histogram_observe(const uint64_t index, const int64_t val) = 0;

    nlohmann::json get_result_in_json(const bool need_latest);
    [[nodiscard]] const std::string& get_group_name() const;
    [[nodiscard]] const std::string& get_instance_name() const;

    void publish_result();
    void gather();

    [[nodiscard]] virtual const CounterStaticInfo& counter_static_info(const uint64_t idx) const {
        return m_static_info->m_counters[idx];
    }
    virtual CounterDynamicInfo& counter_dynamic_info(const uint64_t idx) { return m_counters_dinfo[idx]; }

    [[nodiscard]] virtual const GaugeStaticInfo& gauge_static_info(const uint64_t idx) const {
        return m_static_info->m_gauges[idx];
    }
    virtual GaugeDynamicInfo& gauge_dynamic_info(const uint64_t idx) { return m_gauges_dinfo[idx]; }

    [[nodiscard]] virtual const HistogramStaticInfo& hist_static_info(const uint64_t idx) const {
        return m_static_info->m_histograms[idx];
    }
    virtual HistogramDynamicInfo& hist_dynamic_info(const uint64_t idx) { return m_histograms_dinfo[idx]; }

    [[nodiscard]] virtual uint64_t num_counters() const {
        assert(m_static_info->m_counters.size() == m_counters_dinfo.size());
        return m_counters_dinfo.size();
    }

    [[nodiscard]] virtual uint64_t num_gauges() const {
        assert(m_static_info->m_gauges.size() == m_gauges_dinfo.size());
        return m_gauges_dinfo.size();
    }

    [[nodiscard]] virtual uint64_t num_histograms() const {
        assert(m_static_info->m_histograms.size() == m_histograms_dinfo.size());
        return m_histograms_dinfo.size();
    }

    void attach_gather_cb(const on_gather_cb_t& cb) {
        auto locked{lock()};
        m_on_gather_cb = cb;
    }

    void detach_gather_cb() {
        auto locked{lock()};
        m_on_gather_cb = nullptr;
    }

    void add_child_group(const MetricsGroupImplPtr& child_grp) {
        auto locked{lock()};
        m_child_groups.push_back(child_grp);
    }

    [[nodiscard]] std::string instance_name() const { return m_inst_name; }
    [[nodiscard]] virtual group_impl_type_t impl_type() const = 0;

    virtual void on_register() = 0;

protected:
    virtual void gather_result(const bool need_latest, const counter_gather_cb_t& counter_cb,
                               const gauge_gather_cb_t& gauge_cb, const histogram_gather_cb_t& histogram_cb) = 0;

protected:
    std::string m_inst_name;
    std::mutex m_mutex;
    on_gather_cb_t m_on_gather_cb = nullptr;
    std::shared_ptr< MetricsGroupStaticInfo > m_static_info;

    std::vector< CounterDynamicInfo > m_counters_dinfo;
    std::vector< GaugeDynamicInfo > m_gauges_dinfo;
    std::vector< HistogramDynamicInfo > m_histograms_dinfo;

    std::vector< GaugeValue > m_gauge_values;
    std::vector< MetricsGroupImplPtr > m_child_groups;
};

} // namespace sisl
