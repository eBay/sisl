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
#include "cache.hpp"

Cache::Cache(const char* grp_name) : m_metrics(grp_name) {}

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
