/*
 * Created by Hari Kadayam on Dec-12 2018
 *
 */
#pragma once

#include <set>
#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include <variant>
#include <atomic>
#include <cstdint>
#include <memory>
#include "histogram_buckets.hpp"
#include "utility/thread_buffer.hpp"
#include "prometheus_reporter.hpp"

namespace sisl {
using on_gather_cb_t = std::function< void(void) >;

enum class group_impl_type_t {
    rcu,
    thread_buf_volatile,
    thread_buf_signal,
    atomic,
};

enum _publish_as {
    publish_as_counter,
    publish_as_gauge,
    publish_as_histogram,
};

/****************************** Counter ************************************/
class CounterValue {
public:
    friend class AtomicCounterValue;

    CounterValue() = default;
    void increment(int64_t value = 1) { m_value += value; }
    void decrement(int64_t value = 1) { m_value -= value; }
    int64_t get() const { return m_value; }
    int64_t merge(const CounterValue& other) {
        this->m_value += other.m_value;
        return this->m_value;
    }

private:
    int64_t m_value = 0;
};

class CounterStaticInfo {
    friend class CounterDynamicInfo;

public:
    CounterStaticInfo(const std::string& name, const std::string& desc, const std::string& report_name = "",
                      const metric_label& label_pair = {"", ""});

    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string label_pair() const {
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
                       _publish_as ptype = publish_as_counter);

    void publish(const CounterValue& value);

private:
    std::shared_ptr< ReportCounter >& as_counter() {
        return std::get< std::shared_ptr< ReportCounter > >(m_report_counter_gauge);
    }

    std::shared_ptr< ReportGauge >& as_gauge() {
        return std::get< std::shared_ptr< ReportGauge > >(m_report_counter_gauge);
    }

    bool is_counter_reporter() {
        return std::holds_alternative< std::shared_ptr< ReportCounter > >(m_report_counter_gauge);
    }

private:
    std::variant< std::shared_ptr< ReportCounter >, std::shared_ptr< ReportGauge > > m_report_counter_gauge;
};

/****************************** Gauge ************************************/
class GaugeValue {
public:
    GaugeValue() : m_value(0) {}
    GaugeValue(const std::atomic< int64_t >& oval) : m_value(oval.load(std::memory_order_relaxed)) {}
    GaugeValue(const GaugeValue& other) : m_value(other.get()) {}
    GaugeValue& operator=(const GaugeValue& other) {
        m_value.store(other.get(), std::memory_order_relaxed);
        return *this;
    }
    inline void update(int64_t value) { m_value.store(value, std::memory_order_relaxed); }
    int64_t get() const { return m_value.load(std::memory_order_relaxed); }

private:
    std::atomic< int64_t > m_value;
};

class GaugeStaticInfo {
    friend class MetricsGroup;
    friend class GaugeDynamicInfo;

public:
    GaugeStaticInfo(const std::string& name, const std::string& desc, const std::string& report_name = "",
                    const metric_label& label_pair = {"", ""});

    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string label_pair() const {
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
    void publish(const GaugeValue& value);

private:
    std::shared_ptr< ReportGauge > m_report_gauge;
};

/****************************** Histogram ************************************/
class HistogramValue {
public:
    friend class AtomicHistogramValue;

    void observe(int64_t value, const hist_bucket_boundaries_t& boundaries, uint64_t count = 1) {
        auto lower = std::lower_bound(boundaries.begin(), boundaries.end(), value);
        auto bkt_idx = lower - boundaries.begin();
        m_freqs[bkt_idx] += count;
        m_sum += (value * count);
    }

    void merge(const HistogramValue& other, const hist_bucket_boundaries_t& boundaries) {
        for (auto i = 0U; i < boundaries.size(); i++) {
            this->m_freqs[i] += other.m_freqs[i];
        }
        this->m_sum += other.m_sum;
    }
    auto& get_freqs() const { return m_freqs; }
    int64_t get_sum() const { return m_sum; }

private:
    std::array< int64_t, HistogramBuckets::max_hist_bkts > m_freqs;
    int64_t m_sum = 0;
};

static_assert(std::is_trivially_copyable< HistogramValue >::value, "Expecting HistogramValue to be trivally copyable");

class HistogramStaticInfo {
    friend class HistogramDynamicInfo;

public:
    HistogramStaticInfo(const std::string& name, const std::string& desc, const std::string& report_name = "",
                        const metric_label& label_pair = {"", ""},
                        const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets));

    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string label_pair() const {
        return (!m_label_pair.first.empty() && !m_label_pair.second.empty())
            ? m_label_pair.first + "-" + m_label_pair.second
            : "";
    }

    const hist_bucket_boundaries_t& get_boundaries() const { return m_bkt_boundaries; }

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

    double percentile(const HistogramValue& hvalue, const hist_bucket_boundaries_t&, float pcntl) const;
    int64_t count(const HistogramValue& hvalue) const;
    double average(const HistogramValue& hvalue) const;

    void publish(const HistogramValue& hvalue);

private:
    std::shared_ptr< ReportHistogram >& as_histogram() {
        return std::get< std::shared_ptr< ReportHistogram > >(m_report_histogram_gauge);
    }

    std::shared_ptr< ReportGauge >& as_gauge() {
        return std::get< std::shared_ptr< ReportGauge > >(m_report_histogram_gauge);
    }

    bool is_histogram_reporter() {
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
    static std::shared_ptr< MetricsGroupStaticInfo > create_or_get_info(const std::string& grp_name);

    MetricsGroupStaticInfo(const std::string& grp_name);
    uint64_t register_counter(const std::string& name, const std::string& desc, const std::string& report_name = "",
                              const metric_label& label_pair = {"", ""});

    uint64_t register_gauge(const std::string& name, const std::string& desc, const std::string& report_name = "",
                            const metric_label& label_pair = {"", ""});

    uint64_t register_histogram(const std::string& name, const std::string& desc, const std::string& report_name = "",
                                const metric_label& label_pair = {"", ""},
                                const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets));

    const CounterStaticInfo& get_counter_info(uint64_t index) const;
    const GaugeStaticInfo& get_gauge_info(uint64_t index) const;
    const HistogramStaticInfo& get_histogram_info(uint64_t index) const;

public:
    std::string m_grp_name;
    std::mutex m_mutex;

    std::vector< CounterStaticInfo > m_counters;
    std::vector< GaugeStaticInfo > m_gauges;
    std::vector< HistogramStaticInfo > m_histograms;
    bool m_reg_pending = true;
};

using counter_gather_cb_t = std::function< void(uint64_t, const CounterValue&) >;
using gauge_gather_cb_t = std::function< void(uint64_t, const GaugeValue&) >;
using histogram_gather_cb_t = std::function< void(uint64_t, const HistogramValue&) >;

class MetricsGroupImpl {
private:
    [[nodiscard]] auto lock() { return std::lock_guard< decltype(m_mutex) >(m_mutex); }

public:
    MetricsGroupImpl(const std::string& grp_name, const std::string& inst_name);

    void registration_completed();

    /* Counter */
    uint64_t register_counter(const std::string& name, const std::string& desc, const std::string& report_name = "",
                              const metric_label& label_pair = {"", ""},
                              _publish_as ptype = _publish_as::publish_as_counter);
    uint64_t register_counter(const std::string& name, const std::string& desc, const metric_label& label_pair,
                              _publish_as ptype = publish_as_counter);
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

    virtual void counter_increment(uint64_t index, int64_t val = 1) = 0;
    virtual void counter_decrement(uint64_t index, int64_t val = 1) = 0;

    void gauge_update(uint64_t index, int64_t val);

    virtual void histogram_observe(uint64_t index, int64_t val, uint64_t count) = 0;
    virtual void histogram_observe(uint64_t index, int64_t val) = 0;

    nlohmann::json get_result_in_json(bool need_latest);
    const std::string& get_group_name() const;
    const std::string& get_instance_name() const;

    void publish_result();
    void gather();

    virtual const CounterStaticInfo& counter_static_info(uint64_t idx) const { return m_static_info->m_counters[idx]; }
    virtual CounterDynamicInfo& counter_dynamic_info(uint64_t idx) { return m_counters_dinfo[idx]; }

    virtual const GaugeStaticInfo& gauge_static_info(uint64_t idx) const { return m_static_info->m_gauges[idx]; }
    virtual GaugeDynamicInfo& gauge_dynamic_info(uint64_t idx) { return m_gauges_dinfo[idx]; }

    virtual const HistogramStaticInfo& hist_static_info(uint64_t idx) const { return m_static_info->m_histograms[idx]; }
    virtual HistogramDynamicInfo& hist_dynamic_info(uint64_t idx) { return m_histograms_dinfo[idx]; }

    virtual uint64_t num_counters() const {
        assert(m_static_info->m_counters.size() == m_counters_dinfo.size());
        return m_counters_dinfo.size();
    }

    virtual uint64_t num_gauges() const {
        assert(m_static_info->m_gauges.size() == m_gauges_dinfo.size());
        return m_gauges_dinfo.size();
    }

    virtual uint64_t num_histograms() const {
        assert(m_static_info->m_histograms.size() == m_histograms_dinfo.size());
        return m_histograms_dinfo.size();
    }

    void attach_gather_cb(const on_gather_cb_t& cb) {
        auto locked = lock();
        m_on_gather_cb = cb;
    }

    void detach_gather_cb() {
        auto locked = lock();
        m_on_gather_cb = nullptr;
    }

    virtual group_impl_type_t impl_type() const = 0;

    virtual void on_register() = 0;

protected:
    virtual void gather_result(bool need_latest, const counter_gather_cb_t& counter_cb,
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
};

} // namespace sisl