//
// Created by Kadayam, Hari on 2/5/19.
//

#include "metrics.hpp"
#include <sds_logging/logging.h>

namespace sisl {

#define PROMETHEUS_METRICS_REPORTER

/**************************** MetricsFarm ***********************************/
MetricsFarm& MetricsFarm::getInstance() {
    static MetricsFarm instance;
    return instance;
}

Reporter& MetricsFarm::get_reporter() { return *getInstance().m_reporter; }

MetricsFarm::MetricsFarm() {
#ifdef PROMETHEUS_METRICS_REPORTER
    // m_reporter = std::dynamic_pointer_cast< Reporter >(std::make_unique< PrometheusReporter >());
    m_reporter = std::make_unique< PrometheusReporter >();
#endif
}

void MetricsFarm::register_metrics_group(MetricsGroupPtr mgroup) {
    assert(mgroup != nullptr);
    auto locked = lock();
    mgroup->on_register();
    m_mgroups.insert(mgroup);
}

void MetricsFarm::deregister_metrics_group(MetricsGroupPtr mgroup) {
    assert(mgroup != nullptr);
    auto locked = lock();
    m_mgroups.erase(mgroup);
}

nlohmann::json MetricsFarm::get_result_in_json(bool need_latest) {
    nlohmann::json json;
    auto           locked = lock();
    if (!need_latest) {
        for (auto& mgroup : m_mgroups) {
            json[mgroup->get_group_name()][mgroup->get_instance_name()] = mgroup->get_result_in_json(need_latest);
        }
    } else {
        for (auto& mgroup : m_mgroups) {
            mgroup->prepare_gather();
        }
        urcu_ctl::sync_rcu();
        for (auto& mgroup : m_mgroups) {
            json[mgroup->get_group_name()][mgroup->get_instance_name()] = mgroup->get_result_in_json(need_latest);
        }
    }
    return json;
}

std::string MetricsFarm::get_result_in_json_string(bool need_latest) { return get_result_in_json(need_latest).dump(); }

std::string MetricsFarm::report(ReportFormat format) {
    auto locked = lock();
    for (auto& mgroup : m_mgroups) {
        mgroup->prepare_gather();
    }
    urcu_ctl::sync_rcu();
    for (auto& mgroup : m_mgroups) {
        mgroup->publish_result();
    }

    // Now everything is published to reporter, serialize them
    return m_reporter->serialize(format);
}

/**************************** MetricsGroup ***********************************/
MetricsGroupPtr MetricsGroup::make_group(const char* grp_name, const char* inst_name) {
    return std::make_shared< MetricsGroup >(grp_name, inst_name);
}

MetricsGroup::MetricsGroup(const char* grp_name, const char* inst_name) :
        m_grp_name(grp_name),
        m_inst_name(inst_name) {
    m_gather_pending = false;
}

MetricsGroup::MetricsGroup(const std::string& grp_name, const std::string& inst_name) :
        m_grp_name(grp_name),
        m_inst_name(inst_name) {
    m_gather_pending = false;
}

uint64_t MetricsGroup::register_counter(const std::string& name, const std::string& desc,
                                        const std::string& report_name, const metric_label& label_pair,
                                        _publish_as ptype) {
    auto locked = lock();
    m_counters.emplace_back(name, desc, m_inst_name, report_name, label_pair, ptype);
    return m_counters.size() - 1;
}
uint64_t MetricsGroup::register_counter(const CounterInfo& counter) {
    auto locked = lock();
    m_counters.push_back(counter);
    return m_counters.size() - 1;
}
uint64_t MetricsGroup::register_counter(const std::string& name, const std::string& desc, _publish_as ptype) {
    return register_counter(name, desc, "", {"", ""}, ptype);
}

uint64_t MetricsGroup::register_gauge(const std::string& name, const std::string& desc, const std::string& report_name,
        const metric_label& label_pair) {
    auto locked = lock();
    m_gauges.emplace_back(name, desc, m_inst_name, report_name, label_pair);
    return m_gauges.size() - 1;
}
uint64_t MetricsGroup::register_gauge(const GaugeInfo& gauge) {
    auto locked = lock();
    m_gauges.push_back(gauge);
    return m_gauges.size() - 1;
}

uint64_t MetricsGroup::register_histogram(const std::string& name, const std::string& desc,
                                          const std::string& report_name, const metric_label& label_pair,
                                          const hist_bucket_boundaries_t& bkt_boundaries) {
    auto locked = lock();
    m_histograms.emplace_back(name, desc, m_inst_name, report_name, label_pair, bkt_boundaries);
    m_bkt_boundaries.push_back(bkt_boundaries);
    return m_histograms.size() - 1;
}
uint64_t MetricsGroup::register_histogram(HistogramInfo& hist) {
    auto locked = lock();
    m_histograms.push_back(hist);
    m_bkt_boundaries.push_back(hist.get_boundaries());
    return m_histograms.size() - 1;
}
uint64_t MetricsGroup::register_histogram(const std::string& name, const std::string& desc,
                                          const hist_bucket_boundaries_t& bkt_boundaries) {
    return register_histogram(name, desc, "", {"", ""}, bkt_boundaries);
}

void MetricsGroup::counter_increment(uint64_t index, int64_t val) {
    m_metrics->insertable()->get_counter(index).increment(val);
#if 0
    LOGTRACE("Updating SafeMetrics={} countervalue = {}, index={} to new val = {}", (void*)m_metrics->insertable(),
            (void*)&m_metrics->insertable()->get_counter(index), index,
            m_metrics->insertable()->get_counter(index).get());
#endif
}
void MetricsGroup::counter_decrement(uint64_t index, int64_t val) {
    m_metrics->insertable()->get_counter(index).decrement(val);
}

void MetricsGroup::gauge_update(uint64_t index, int64_t val) { m_gauges[index].m_gauge.update(val); }

void MetricsGroup::histogram_observe(uint64_t index, int64_t val) {
    m_metrics->insertable()->get_histogram(index).observe(val, m_bkt_boundaries[index]);
#if 0
    LOGTRACE("Thread={}: Updating SafeMetrics={} histvalue = {}, index={}",
           pthread_self(), m_metrics->insertable(), &m_metrics->insertable()->get_histogram(index), index);
#endif
}

const CounterInfo&   MetricsGroup::get_counter_info(uint64_t index) const { return m_counters[index]; }
const GaugeInfo&     MetricsGroup::get_gauge_info(uint64_t index) const { return m_gauges[index]; }
const HistogramInfo& MetricsGroup::get_histogram_info(uint64_t index) const { return m_histograms[index]; }

nlohmann::json MetricsGroup::get_result_in_json(bool need_latest) {
    auto           locked = lock();
    nlohmann::json json;
    nlohmann::json counter_entries;
    nlohmann::json gauge_entries;
    nlohmann::json hist_entries;

    gather_result(
        need_latest,
        [&counter_entries](CounterInfo& c, const CounterValue& result) { counter_entries[c.desc()] = result.get(); },
        [&gauge_entries](GaugeInfo& g) { gauge_entries[g.desc()] = g.get(); },
        [&hist_entries](HistogramInfo& h, const HistogramValue& result) {
            std::stringstream ss;
            ss << h.average(result) << " / " << h.percentile(result, 50) << " / " << h.percentile(result, 95) << " / "
               << h.percentile(result, 99);
            hist_entries[h.desc()] = ss.str();
        });

    json["Counters"] = counter_entries;
    json["Gauges"] = gauge_entries;
    json["Histograms percentiles (usecs) avg/50/95/99"] = hist_entries;
    return json;
}

void MetricsGroup::prepare_gather() {
    auto locked = lock();
    m_metrics->prepare_rotate();
    m_gather_pending = true;
}

void MetricsGroup::publish_result() {
    auto locked = lock();
    gather_result(true, /* need_latest */
                  [](CounterInfo& c, const CounterValue& result) { c.publish(result); },
                  [](GaugeInfo& g) { g.publish(); },
                  [](HistogramInfo& h, const HistogramValue& result) { h.publish(result); });
}

const std::string& MetricsGroup::get_group_name() const { return m_grp_name; }
const std::string& MetricsGroup::get_instance_name() const { return m_inst_name; }

void MetricsGroup::on_register() {
    m_metrics = std::make_unique< ThreadSafeMetrics >(m_histograms, m_counters.size(), m_histograms.size());
}

void MetricsGroup::gather_result(bool need_latest, std::function< void(CounterInfo&, const CounterValue&) > counter_cb,
                                 std::function< void(GaugeInfo&) >                            gauge_cb,
                                 std::function< void(HistogramInfo&, const HistogramValue&) > histogram_cb) {

    SafeMetrics* smetrics;
    if (need_latest) {
        if (m_gather_pending) {
            m_gather_pending = false;
            smetrics = m_metrics->deferred();
        } else {
            smetrics = m_metrics->now();
        }
    } else {
        smetrics = m_metrics->delayed();
    }

    for (auto i = 0U; i < m_counters.size(); i++) {
        counter_cb(m_counters[i], smetrics->get_counter(i));
    }

    for (auto i = 0U; i < m_gauges.size(); i++) {
        gauge_cb(m_gauges[i]);
    }

    for (auto i = 0U; i < m_histograms.size(); i++) {
        histogram_cb(m_histograms[i], smetrics->get_histogram(i));
    }
}

/***************************** CounterInfo **************************/
CounterInfo::CounterInfo(const std::string& name, const std::string& desc, const std::string& instance_name,
                         _publish_as ptype) :
        CounterInfo(name, desc, instance_name, "", {"", ""}, ptype) {}

CounterInfo::CounterInfo(const std::string& name, const std::string& desc, const std::string& instance_name,
                         const std::string& report_name, const metric_label& label_pair, _publish_as ptype) :
        m_name(report_name.empty() ? name : report_name),
        m_desc(desc) {
    if (ptype == publish_as_counter) {
        assert(m_report_counter == nullptr);
        m_report_counter = MetricsFarm::get_reporter().add_counter(m_name, desc, instance_name, label_pair);
    } else if (ptype == publish_as_gauge) {
        m_report_gauge = MetricsFarm::get_reporter().add_gauge(m_name, desc, instance_name, label_pair);
    }

    if (!label_pair.first.empty() && !label_pair.second.empty()) {
        m_label_pair = label_pair;
    }
}

void CounterInfo::publish(const CounterValue& value) {
    if (m_report_counter != nullptr) {
        m_report_counter->set_value(value.get());
    } else if (m_report_gauge != nullptr) {
        m_report_gauge->set_value(value.get());
    } else {
        assert(0);
    }
}

/***************************** GaugeInfo **************************/
GaugeInfo::GaugeInfo(const std::string& name, const std::string& desc, const std::string& instance_name,
                     const std::string& report_name, const metric_label& label_pair) :
        m_name(report_name.empty() ? name : report_name),
        m_desc(desc) {
    m_report_gauge = MetricsFarm::get_reporter().add_gauge(m_name, desc, instance_name, label_pair);
    if (!label_pair.first.empty() && !label_pair.second.empty()) {
        m_label_pair = label_pair;
    }
}

void GaugeInfo::publish() { m_report_gauge->set_value((double)m_gauge.get()); }

/***************************** HistogramInfo **************************/
HistogramInfo::HistogramInfo(const std::string& name, const std::string& desc, const std::string& instance_name,
                             const std::string& report_name, const metric_label& label_pair,
                             const hist_bucket_boundaries_t& bkt_boundaries) :
        m_name(report_name.empty() ? name : report_name),
        m_desc(desc),
        m_bkt_boundaries(bkt_boundaries) {
    m_report_histogram =
        MetricsFarm::get_reporter().add_histogram(m_name, desc, instance_name, bkt_boundaries, label_pair);
    if (!label_pair.first.empty() && !label_pair.second.empty()) {
        m_label_pair = label_pair;
    }
}

double HistogramInfo::percentile(const HistogramValue& hvalue, float pcntl) const {
    std::array< int64_t, HistogramBuckets::max_hist_bkts > cum_freq;
    int64_t                                                fcount = 0;
    for (auto i = 0U; i < HistogramBuckets::max_hist_bkts; i++) {
        fcount += (hvalue.get_freqs())[i];
        cum_freq[i] = fcount;
    }

    int64_t pnum = fcount * pcntl / 100;
    auto    i = (std::lower_bound(cum_freq.begin(), cum_freq.end(), pnum)) - cum_freq.begin();
    if ((hvalue.get_freqs())[i] == 0)
        return 0;
    auto   Yl = i == 0 ? 0 : m_bkt_boundaries[i - 1];
    auto   ith_cum_freq = (i == 0) ? 0 : cum_freq[i - 1];
    double Yp = Yl + (((pnum - ith_cum_freq) * i) / (hvalue.get_freqs())[i]);
    return Yp;

    /* Formula:
        Yp = lower bound of i-th bucket + ((pn - cumfreq[i-1]) * i ) / freq[i]
        where
            pn = (cnt * percentile)/100
            i  = matched index of pnum in cum_freq
     */
}

int64_t HistogramInfo::count(const HistogramValue& hvalue) const {
    int64_t cnt = 0;
    for (auto i = 0U; i < HistogramBuckets::max_hist_bkts; i++) {
        cnt += (hvalue.get_freqs())[i];
    }
    return cnt;
}

double HistogramInfo::average(const HistogramValue& hvalue) const {
    auto cnt = count(hvalue);
    return (cnt ? hvalue.get_sum() / cnt : 0);
}

void HistogramInfo::publish(const HistogramValue& hvalue) {
    auto arr = hvalue.get_freqs();
    m_report_histogram->set_value(std::vector<double>(arr.begin(), arr.end()), hvalue.get_sum());
}
} // namespace sisl