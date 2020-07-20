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
#include "histogram_buckets.hpp"
#include "utility/thread_buffer.hpp"
#include "prometheus_reporter.hpp"

namespace sisl {
using on_gather_cb_t = std::function< void(void) >;

enum class group_impl_type_t {
    rcu,
    thread_buf_volatile,
    thread_buf_signal,
};

enum _publish_as {
    publish_as_counter,
    publish_as_gauge,
    publish_as_histogram,
};

class CounterValue {
public:
    CounterValue() = default;
    void increment(int64_t value = 1) { m_value += value; }
    void decrement(int64_t value = 1) { m_value -= value; }
    int64_t get() const { return m_value; }
    int64_t merge(const CounterValue& other) {
        this->m_value += other.m_value;
        return this->m_value;
    }

private:
    volatile int64_t m_value = 0;
};

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

class HistogramValue {
public:
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
    volatile int64_t m_sum = 0;
};

static_assert(std::is_trivially_copyable< HistogramValue >::value, "Expecting HistogramValue to be trivally copyable");

class CounterInfo {
public:
    CounterInfo(const std::string& name, const std::string& desc, const std::string& instance_name,
                _publish_as ptype = publish_as_counter);
    CounterInfo(const std::string& name, const std::string& desc, const std::string& instance_name,
                const std::string& report_name, const metric_label& label_pair, _publish_as ptype = publish_as_counter);

    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string label_pair() const {
        return (!m_label_pair.first.empty() && !m_label_pair.second.empty())
            ? m_label_pair.first + "-" + m_label_pair.second
            : "";
    }
    void publish(const CounterValue& value);

private:
    std::string m_name;
    std::string m_desc;
    std::pair< std::string, std::string > m_label_pair;
    std::shared_ptr< ReportCounter > m_report_counter;
    std::shared_ptr< ReportGauge > m_report_gauge;
};

class GaugeInfo {
    friend class MetricsGroup;

public:
    GaugeInfo(const std::string& name, const std::string& desc, const std::string& instance_name,
              const std::string& report_name = "", const metric_label& label_pair = {"", ""});

    uint64_t get() const { return m_gauge.get(); };
    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string label_pair() const {
        return (!m_label_pair.first.empty() && !m_label_pair.second.empty())
            ? m_label_pair.first + "-" + m_label_pair.second
            : "";
    }
    void publish();
    inline GaugeValue& value() { return m_gauge; }

private:
    GaugeValue m_gauge;
    const std::string m_name;
    const std::string m_desc;
    std::pair< std::string, std::string > m_label_pair;
    std::shared_ptr< ReportGauge > m_report_gauge;
};

class HistogramInfo {
public:
    HistogramInfo(const std::string& name, const std::string& desc, const std::string& instance_name,
                  const std::string& report_name = "", const metric_label& label_pair = {"", ""},
                  const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets));

    double percentile(const HistogramValue& hvalue, float pcntl) const;
    int64_t count(const HistogramValue& hvalue) const;
    double average(const HistogramValue& hvalue) const;

    std::string name() const { return m_name; }
    std::string desc() const { return m_desc; }
    std::string label_pair() const {
        return (!m_label_pair.first.empty() && !m_label_pair.second.empty())
            ? m_label_pair.first + "-" + m_label_pair.second
            : "";
    }

    void publish(const HistogramValue& hvalue);
    const hist_bucket_boundaries_t& get_boundaries() const { return m_bkt_boundaries; }

private:
    const std::string m_name;
    const std::string m_desc;
    std::pair< std::string, std::string > m_label_pair;
    const hist_bucket_boundaries_t& m_bkt_boundaries;
    std::shared_ptr< ReportHistogram > m_report_histogram;
};

class MetricsGroupImpl;
using MetricsGroupImplPtr = std::shared_ptr< MetricsGroupImpl >;

class MetricsGroupImpl {
    friend class MetricsFarm;
    friend class MetricsResult;

private:
    [[nodiscard]] auto lock() { return std::lock_guard< decltype(m_mutex) >(m_mutex); }

public:
    MetricsGroupImpl(const char* grp_name, const char* inst_name);
    MetricsGroupImpl(const std::string& grp_name, const std::string& inst_name);

    uint64_t register_counter(const std::string& name, const std::string& desc, const std::string& report_name = "",
                              const metric_label& label_pair = {"", ""}, _publish_as ptype = publish_as_counter);
    uint64_t register_counter(const std::string& name, const std::string& desc, _publish_as ptype);

    uint64_t register_counter(const CounterInfo& counter);

    uint64_t register_gauge(const std::string& name, const std::string& desc, const std::string& report_name = "",
                            const metric_label& label_pair = {"", ""});
    uint64_t register_gauge(const GaugeInfo& gauge);

    uint64_t register_histogram(const std::string& name, const std::string& desc, const std::string& report_name = "",
                                const metric_label& label_pair = {"", ""},
                                const hist_bucket_boundaries_t& bkt_boundaries = HistogramBucketsType(DefaultBuckets));
    uint64_t register_histogram(HistogramInfo& hist);
    uint64_t register_histogram(const std::string& name, const std::string& desc,
                                const hist_bucket_boundaries_t& bkt_boundaries);

    virtual void counter_increment(uint64_t index, int64_t val = 1) = 0;
    virtual void counter_decrement(uint64_t index, int64_t val = 1) = 0;
    virtual void histogram_observe(uint64_t index, int64_t val, uint64_t count) = 0;
    virtual void histogram_observe(uint64_t index, int64_t val) = 0;
    void gauge_update(uint64_t index, int64_t val);

    const CounterInfo& get_counter_info(uint64_t index) const;
    const GaugeInfo& get_gauge_info(uint64_t index) const;
    const HistogramInfo& get_histogram_info(uint64_t index) const;

    nlohmann::json get_result_in_json(bool need_latest);

    void publish_result();
    void gather();
    const std::string& get_group_name() const;
    const std::string& get_instance_name() const;

    void attach_gather_cb(const on_gather_cb_t& cb) {
        auto locked = lock();
        m_on_gather_cb = cb;
    }

    void detach_gather_cb() {
        auto locked = lock();
        m_on_gather_cb = nullptr;
    }

    virtual group_impl_type_t impl_type() const = 0;

protected:
    virtual void on_register() = 0;
    virtual void gather_result(bool need_latest, std::function< void(CounterInfo&, const CounterValue&) > counter_cb,
                               std::function< void(GaugeInfo&) > gauge_cb,
                               std::function< void(HistogramInfo&, const HistogramValue&) > histogram_cb) = 0;

protected:
    std::string m_grp_name;
    std::string m_inst_name;
    std::mutex m_mutex;
    on_gather_cb_t m_on_gather_cb = nullptr;

    std::vector< CounterInfo > m_counters;
    std::vector< GaugeInfo > m_gauges;
    std::vector< HistogramInfo > m_histograms;
    std::vector< std::reference_wrapper< const hist_bucket_boundaries_t > > m_bkt_boundaries;
};
} // namespace sisl