//
// Created by Kadayam, Hari on 1/10/19.
//
#include "cache.hpp"

Cache::Cache(const char *grp_name) : m_metrics(grp_name) {}

void Cache::update() {
    GAUGE_UPDATE(m_metrics, cache_size, 1);
    GAUGE_UPDATE(m_metrics, cache_size, 4);
    GAUGE_UPDATE(m_metrics, cache_eviction_pct, 8);
    GAUGE_UPDATE(m_metrics, cache_writes_rate, 2);

    HISTOGRAM_OBSERVE(m_metrics, cache_write_latency, 100);
    HISTOGRAM_OBSERVE(m_metrics, cache_write_latency, 150);
    HISTOGRAM_OBSERVE(m_metrics, cache_read_latency, 150);
    HISTOGRAM_OBSERVE(m_metrics, cache_delete_latency, 200);
}
