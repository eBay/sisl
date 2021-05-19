//
// Created by Kadayam, Hari on 2/5/19.
//

#include <algorithm>

#if defined __clang__ or defined __GNUC__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
#endif
    #include <folly/Synchronized.h>
#if defined __clang__ or defined __GNUC__
    #pragma GCC diagnostic pop
#endif

#include <fmt/format.h>
#include <sds_logging/logging.h>

#include "metrics_group_impl.hpp"
#include "metrics.hpp"
#include "metrics_tlocal.hpp"

namespace sisl {

#define PROMETHEUS_METRICS_REPORTER

MetricsGroupImplPtr MetricsGroup::make_group(const std::string& grp_name, const std::string& inst_name,
                                             group_impl_type_t type) {
    if (type == group_impl_type_t::thread_buf_signal) {
        return std::dynamic_pointer_cast< MetricsGroupImpl >(
            std::make_shared< ThreadBufferMetricsGroup >(grp_name, inst_name));
    } else if (type == group_impl_type_t::rcu) {
        return std::dynamic_pointer_cast< MetricsGroupImpl >(
            std::make_shared< WisrBufferMetricsGroup >(grp_name, inst_name));
    } else if (type == group_impl_type_t::atomic) {
        return std::dynamic_pointer_cast< MetricsGroupImpl >(
            std::make_shared< AtomicMetricsGroup >(grp_name, inst_name));
    } else {
        return nullptr;
    }
}

void MetricsGroup::register_me_to_farm() {
    MetricsFarm::getInstance().register_metrics_group(m_impl_ptr);
    m_is_registered.store(true);
}

void MetricsGroup::deregister_me_from_farm() {
    if (m_is_registered.load() && MetricsFarm::is_initialized()) {
        MetricsFarm::getInstance().deregister_metrics_group(m_impl_ptr);
        m_is_registered.store(false);
    }
}

void MetricsGroup::register_me_to_parent(MetricsGroup* parent) {
    parent->m_impl_ptr->add_child_group(m_impl_ptr);

    // We don't need to add to farm list, we are added to the parent.
    MetricsFarm::getInstance().register_metrics_group(m_impl_ptr, false /* add_to_farm_list */);
}

nlohmann::json MetricsGroup::get_result_in_json(bool need_latest) {
    return m_impl_ptr->get_result_in_json(need_latest);
}

void MetricsGroup::gather() { m_impl_ptr->gather(); }

void MetricsGroup::attach_gather_cb(const on_gather_cb_t& cb) { m_impl_ptr->attach_gather_cb(cb); }
void MetricsGroup::detach_gather_cb() { m_impl_ptr->detach_gather_cb(); }

/****************************MetricsGroupStaticInfo Section ********************************************/
MetricsGroupStaticInfo::MetricsGroupStaticInfo(const std::string& grp_name) : m_grp_name(grp_name) {}

uint64_t MetricsGroupStaticInfo::register_counter(const std::string& name, const std::string& desc,
                                                  const std::string& report_name, const metric_label& label_pair) {
    m_counters.emplace_back(name, desc, report_name, label_pair);
    return m_counters.size() - 1;
}

uint64_t MetricsGroupStaticInfo::register_gauge(const std::string& name, const std::string& desc,
                                                const std::string& report_name, const metric_label& label_pair) {
    m_gauges.emplace_back(name, desc, report_name, label_pair);
    return m_gauges.size() - 1;
}

uint64_t MetricsGroupStaticInfo::register_histogram(const std::string& name, const std::string& desc,
                                                    const std::string& report_name, const metric_label& label_pair,
                                                    const hist_bucket_boundaries_t& bkt_boundaries) {
    m_histograms.emplace_back(name, desc, report_name, label_pair, bkt_boundaries);
    return m_histograms.size() - 1;
}

std::shared_ptr< MetricsGroupStaticInfo > MetricsGroupStaticInfo::create_or_get_info(const std::string& grp_name) {
    static folly::Synchronized< std::unordered_map< std::string, std::shared_ptr< MetricsGroupStaticInfo > > > _grp_map;

    std::shared_ptr< MetricsGroupStaticInfo > ret;

    _grp_map.withWLock([&grp_name, &ret](auto& m) {
        auto it_pair{m.try_emplace(grp_name, std::make_shared< MetricsGroupStaticInfo >(grp_name))};
        ret = it_pair.first->second;
    });
    return ret;
}

/****************************MetricsGroupImpl Section ********************************************/
MetricsGroupImpl::MetricsGroupImpl(const std::string& grp_name, const std::string& inst_name) {
    m_inst_name = MetricsFarm::getInstance().ensure_unique(grp_name, inst_name);
    m_static_info = MetricsGroupStaticInfo::create_or_get_info(grp_name);
    m_static_info->m_mutex.lock();
    // The metrics group info is locked and exclusive till all registrations are completed by this instance
}

void MetricsGroupImpl::registration_completed() {
    m_gauge_values.resize(m_static_info->m_gauges.size(), GaugeValue());
    m_static_info->m_reg_pending = false;
    m_static_info->m_mutex.unlock();
}

uint64_t MetricsGroupImpl::register_counter(const std::string& name, const std::string& desc,
                                            const std::string& report_name, const metric_label& label_pair,
                                            _publish_as ptype) {
    const auto idx = m_counters_dinfo.size();
    if (m_static_info->m_reg_pending) {
        [[maybe_unused]] auto s_idx = m_static_info->register_counter(name, desc, report_name, label_pair);
        assert(idx == s_idx);
    }
    m_counters_dinfo.emplace_back(m_static_info->m_counters[idx], m_inst_name, ptype);
    return idx;
}

uint64_t MetricsGroupImpl::register_counter(const std::string& name, const std::string& desc,
                                            const metric_label& label_pair, _publish_as ptype) {
    return register_counter(name, desc, "", label_pair, ptype);
}

uint64_t MetricsGroupImpl::register_counter(const std::string& name, const std::string& desc, _publish_as ptype) {
    return register_counter(name, desc, "", {"", ""}, ptype);
}

uint64_t MetricsGroupImpl::register_gauge(const std::string& name, const std::string& desc,
                                          const std::string& report_name, const metric_label& label_pair) {
    const auto idx = m_gauges_dinfo.size();
    if (m_static_info->m_reg_pending) {
        [[maybe_unused]] auto s_idx = m_static_info->register_gauge(name, desc, report_name, label_pair);
        assert(idx == s_idx);
    }
    m_gauges_dinfo.emplace_back(m_static_info->m_gauges[idx], m_inst_name);
    return idx;
}

uint64_t MetricsGroupImpl::register_gauge(const std::string& name, const std::string& desc,
                                          const metric_label& label_pair) {
    return register_gauge(name, desc, "", label_pair);
}

uint64_t MetricsGroupImpl::register_histogram(const std::string& name, const std::string& desc,
                                              const std::string& report_name, const metric_label& label_pair,
                                              const hist_bucket_boundaries_t& bkt_boundaries, _publish_as ptype) {
    const auto idx = m_histograms_dinfo.size();
    if (m_static_info->m_reg_pending) {
        [[maybe_unused]] auto s_idx =
            m_static_info->register_histogram(name, desc, report_name, label_pair, bkt_boundaries);
        assert(idx == s_idx);
    }
    m_histograms_dinfo.emplace_back(m_static_info->m_histograms[idx], m_inst_name, ptype);
    return idx;
}

uint64_t MetricsGroupImpl::register_histogram(const std::string& name, const std::string& desc,
                                              const metric_label& label_pair,
                                              const hist_bucket_boundaries_t& bkt_boundaries, _publish_as ptype) {
    return register_histogram(name, desc, "", label_pair, bkt_boundaries, ptype);
}

uint64_t MetricsGroupImpl::register_histogram(const std::string& name, const std::string& desc,
                                              const hist_bucket_boundaries_t& bkt_boundaries, _publish_as ptype) {
    return register_histogram(name, desc, "", {"", ""}, bkt_boundaries, ptype);
}

uint64_t MetricsGroupImpl::register_histogram(const std::string& name, const std::string& desc, _publish_as ptype) {
    return register_histogram(name, desc, "", {"", ""}, HistogramBucketsType(DefaultBuckets), ptype);
}

void MetricsGroupImpl::gauge_update(uint64_t index, int64_t val) { m_gauge_values[index].update(val); }

nlohmann::json MetricsGroupImpl::get_result_in_json(bool need_latest) {
    auto locked = lock();
    nlohmann::json json;
    nlohmann::json counter_entries;
    nlohmann::json gauge_entries;
    nlohmann::json hist_entries;

    if (m_on_gather_cb) { m_on_gather_cb(); }
    gather_result(
        need_latest,
        [&counter_entries, this](uint64_t idx, const CounterValue& result) {
            counter_entries[counter_static_info(idx).desc()] = result.get();
        },
        [&gauge_entries, this](uint64_t idx, const GaugeValue& result) {
            gauge_entries[gauge_static_info(idx).desc()] = result.get();
        },
        [&hist_entries, this](uint64_t idx, const HistogramValue& result) {
            HistogramDynamicInfo& h = hist_dynamic_info(idx);
            if (h.is_histogram_reporter()) {
                hist_entries[hist_static_info(idx).desc()] =
                    fmt::format("{:#} / {:#} / {:#} / {:#}", h.average(result),
                                h.percentile(result, hist_static_info(idx).get_boundaries(), 50),
                                h.percentile(result, hist_static_info(idx).get_boundaries(), 95),
                                h.percentile(result, hist_static_info(idx).get_boundaries(), 99));
            } else {
                hist_entries[hist_static_info(idx).desc()] = std::to_string(h.average(result));
            }
        });

    json["Counters"] = counter_entries;
    json["Gauges"] = gauge_entries;
    json["Histograms percentiles (usecs) avg/50/95/99"] = hist_entries;

    for (auto& cg : m_child_groups) {
        json[cg->m_inst_name] = cg->get_result_in_json(need_latest);
    }
    return json;
}

void MetricsGroupImpl::publish_result() {
    auto locked = lock();
    if (m_on_gather_cb) { m_on_gather_cb(); }
    gather_result(
        true, /* need_latest */
        [this](uint64_t idx, const CounterValue& result) { counter_dynamic_info(idx).publish(result); }, // Counter
        [this](uint64_t idx, const GaugeValue& result) { gauge_dynamic_info(idx).publish(result); },     // Gauge
        [this](uint64_t idx, const HistogramValue& result) { hist_dynamic_info(idx).publish(result); }); // Histogram

    // Call child group publish result
    for (auto& cg : m_child_groups) {
        cg->publish_result();
    }
}

void MetricsGroupImpl::gather() {
    auto locked = lock();
    if (m_on_gather_cb) { m_on_gather_cb(); }
    gather_result(
        true, /* need_latest */
        []([[maybe_unused]] uint64_t idx, [[maybe_unused]] const CounterValue& result) {},
        []([[maybe_unused]] uint64_t idx, [[maybe_unused]] const GaugeValue& result) {},
        []([[maybe_unused]] uint64_t idx, [[maybe_unused]] const HistogramValue& result) {});

    for (auto& cg : m_child_groups) {
        cg->gather();
    }
}

const std::string& MetricsGroupImpl::get_group_name() const { return m_static_info->m_grp_name; }
const std::string& MetricsGroupImpl::get_instance_name() const { return m_inst_name; }

/***************************** CounterStaticInfo **************************/
CounterStaticInfo::CounterStaticInfo(const std::string& name, const std::string& desc, const std::string& report_name,
                                     const metric_label& label_pair) :
        m_name(report_name.empty() ? name : report_name),
        m_desc(desc) {
    if (!label_pair.first.empty() && !label_pair.second.empty()) { m_label_pair = label_pair; }
}

CounterDynamicInfo::CounterDynamicInfo(const CounterStaticInfo& static_info, const std::string& instance_name,
                                       _publish_as ptype) {
    if (ptype == _publish_as::publish_as_counter) {
        m_report_counter_gauge = MetricsFarm::get_reporter().add_counter(static_info.m_name, static_info.m_desc,
                                                                         instance_name, static_info.m_label_pair);
    } else if (ptype == _publish_as::publish_as_gauge) {
        m_report_counter_gauge = MetricsFarm::get_reporter().add_gauge(static_info.m_name, static_info.m_desc,
                                                                       instance_name, static_info.m_label_pair);
    }
}

void CounterDynamicInfo::publish(const CounterValue& value) {
    if (is_counter_reporter()) {
        as_counter()->set_value(value.get());
    } else {
        as_gauge()->set_value(value.get());
    }
}

/***************************** GaugeStaticInfo **************************/
GaugeStaticInfo::GaugeStaticInfo(const std::string& name, const std::string& desc, const std::string& report_name,
                                 const metric_label& label_pair) :
        m_name(report_name.empty() ? name : report_name),
        m_desc(desc) {
    if (!label_pair.first.empty() && !label_pair.second.empty()) { m_label_pair = label_pair; }
}

GaugeDynamicInfo::GaugeDynamicInfo(const GaugeStaticInfo& static_info, const std::string& instance_name) {
    m_report_gauge = MetricsFarm::get_reporter().add_gauge(static_info.m_name, static_info.m_desc, instance_name,
                                                           static_info.m_label_pair);
}

void GaugeDynamicInfo::publish(const GaugeValue& value) { m_report_gauge->set_value((double)value.get()); }

/***************************** HistogramStaticInfo **************************/
HistogramStaticInfo::HistogramStaticInfo(const std::string& name, const std::string& desc,
                                         const std::string& report_name, const metric_label& label_pair,
                                         const hist_bucket_boundaries_t& bkt_boundaries) :
        m_name(report_name.empty() ? name : report_name),
        m_desc(desc),
        m_bkt_boundaries(bkt_boundaries) {
    if (!label_pair.first.empty() && !label_pair.second.empty()) { m_label_pair = label_pair; }
}

HistogramDynamicInfo::HistogramDynamicInfo(const HistogramStaticInfo& static_info, const std::string& instance_name,
                                           _publish_as ptype) {
    if (ptype == _publish_as::publish_as_histogram) {
        m_report_histogram_gauge =
            MetricsFarm::get_reporter().add_histogram(static_info.m_name, static_info.m_desc, instance_name,
                                                      static_info.get_boundaries(), static_info.m_label_pair);
    } else {
        m_report_histogram_gauge = MetricsFarm::get_reporter().add_gauge(static_info.m_name, static_info.m_desc,
                                                                         instance_name, static_info.m_label_pair);
    }
}

void HistogramDynamicInfo::publish(const HistogramValue& hvalue) {
    if (is_histogram_reporter()) {
        const auto arr = hvalue.get_freqs();
        as_histogram()->set_value(std::vector< double >(arr.cbegin(), arr.cend()), hvalue.get_sum());
    } else {
        as_gauge()->set_value(average(hvalue));
    }
}

double HistogramDynamicInfo::percentile(const HistogramValue& hvalue, const hist_bucket_boundaries_t& bkt_boundaries,
                                        const float pcntl) const {
    assert((pcntl > 0.0f) && (pcntl <= 100.0f));
    std::array< int64_t, HistogramBuckets::max_hist_bkts > cum_freq;
    int64_t fcount{0};
    const auto& freqs{hvalue.get_freqs()};
    for (size_t i{0}; i < HistogramBuckets::max_hist_bkts; ++i) {
        fcount += freqs[i];
        cum_freq[i] = fcount;
    }

    const int64_t pnum{static_cast< int64_t >(fcount * (pcntl / 100.0f))};
    const auto itr{std::lower_bound(std::cbegin(cum_freq), std::cend(cum_freq), pnum)};
    if (itr == std::cend(cum_freq)) return 0.0;
    const auto index{std::distance(std::cbegin(cum_freq), itr)};
    if (freqs[index] == 0) return 0.0;
    const auto Yl{(index == 0) ? 0.0 : bkt_boundaries[index - 1]};
    const auto ith_cum_freq{(index == 0) ? static_cast< int64_t >(0) : cum_freq[index - 1]};
    const double Yp{Yl + static_cast< double >((pnum - ith_cum_freq) * index) / static_cast< double >(freqs[index])};
    return Yp;

    /* Formula:
        Yp = lower bound of i-th bucket + ((pn - cumfreq[i-1]) * i ) / freq[i]
        where
            pn = (cnt * percentile)/100
            i  = matched index of pnum in cum_freq
     */
}

int64_t HistogramDynamicInfo::count(const HistogramValue& hvalue) const {
    const auto& freqs{hvalue.get_freqs()};
    return std::accumulate(std::cbegin(freqs), std::cend(freqs), static_cast< int64_t >(0));
}

double HistogramDynamicInfo::average(const HistogramValue& hvalue) const {
    const auto cnt = count(hvalue);
    return (cnt ? static_cast< double >(hvalue.get_sum()) / static_cast< double >(cnt) : 0.0);
}
} // namespace sisl
