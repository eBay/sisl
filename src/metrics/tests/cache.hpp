//
// Created by Kadayam, Hari on 1/10/19.
//

#include "metrics.hpp"

#ifndef ASYNC_HTTP_CACHE_HPP
#define ASYNC_HTTP_CACHE_HPP

using namespace sisl;

class CacheMetrics : public MetricsGroupWrapper {
public:
    explicit CacheMetrics(const char* grp_name) : MetricsGroupWrapper(grp_name) {
        REGISTER_GAUGE(cache_size, "cache_size", "");
        REGISTER_GAUGE(cache_eviction_pct, "cache_eviction_pct", "");
        REGISTER_GAUGE(cache_writes_rate, "cache_writes_rate", "");

        REGISTER_HISTOGRAM(cache_write_latency, "cache_write_latency", "");
        REGISTER_HISTOGRAM(cache_read_latency, "cache_read_latency", "");
        REGISTER_HISTOGRAM(cache_delete_latency, "cache_delete_latency", "");

        register_me_to_farm();
    }
};

class Cache {
private:
    CacheMetrics m_metrics;

public:
    Cache(const char* grp_name);
    void update();
};
#endif // ASYNC_HTTP_CACHE_HPP
