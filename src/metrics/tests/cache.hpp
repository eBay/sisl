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
