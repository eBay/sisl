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
#ifndef METRICS_HISTOGRAM_BUCKETS_HPP
#define METRICS_HISTOGRAM_BUCKETS_HPP

#include <vector>
#include <cstdint>
#include <limits> // std::numeric_limits

namespace sisl {
using hist_bucket_boundaries_t = std::vector< double >;

/* For any new histogram buckets, define a name and its values here */
#define HIST_BKTS_TYPES                                                                                                \
    X(DefaultBuckets, 10, 40, 70, 100, 160, 220, 280, 340, 400, 475, 600, 750, 900, 1100, 1400, 1700, 2000, 3500,      \
      5000, 6500, 8000, 10000, 13000, 16000, 20000, 50000, 80000, 100000, 150000, 180000, 200000, 500000, 2000000,     \
      3000000, 4000000)                                                                                                \
                                                                                                                       \
    X(OpLatecyBuckets, 10, 50, 100, 150, 200, 300, 400, 500, 750, 1000, 1500, 2000, 5000, 10000, 20000, 50000, 100000, \
      200000, 300000, 2000000)                                                                                         \
    X(LowResolutionLatecyBuckets, 100, 500, 1000, 5000, 10000, 50000, 100000, 200000, 300000, 2000000)                 \
                                                                                                                       \
    X(ExponentialOfTwoBuckets, 1, ipow2(4), ipow2(7), ipow2(10), ipow2(13), ipow2(16), ipow2(19), ipow2(22),           \
      ipow2(25), ipow2(28), ipow2(31))                                                                                 \
                                                                                                                       \
    X(OpSizeBuckets, ipow2(12), ipow2(13), ipow2(16), ipow2(20), ipow2(22))                                            \
                                                                                                                       \
    X(LinearUpto64Buckets, 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64)                            \
                                                                                                                       \
    X(SteppedUpto32Buckets, 0, 1, 4, 16, 32)                                                                           \
                                                                                                                       \
    X(PercentileBuckets, 10, 20, 30, 40, 50, 60, 70, 80, 90)                                                           \
                                                                                                                       \
    X(LinearUpto128Buckets, 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88,   \
      92, 96, 100, 104, 108, 112, 116, 120, 124, 128)                                                                  \
                                                                                                                       \
    X(SingleValueBucket, std::numeric_limits< double >::max())

template < typename... V >
constexpr size_t _hist_bkt_count([[maybe_unused]] V&&... v) {
    return sizeof...(V);
}

template < typename... V >
constexpr size_t _get_max_hist_bkts(V&&... v) {
    size_t max_size = 0;
    for (auto i : {v...}) {
        if (i > max_size) max_size = i;
    }
    return max_size;
}

constexpr int64_t ipow2(const int64_t exponent) { return exponent == 0 ? 1 : 2 * ipow2(exponent - 1); }

#define HistogramBucketsType(name) (sisl::HistogramBuckets::getInstance().name)

// to define the histogram buckets used for various metrics
class HistogramBuckets {
public:
    static HistogramBuckets& getInstance() {
        static HistogramBuckets instance;
        return instance;
    }

    HistogramBuckets(const HistogramBuckets&) = delete;
    void operator=(const HistogramBuckets&) = delete;

#define X(name, ...) _hist_bkt_count(__VA_ARGS__),
    static const constexpr size_t max_hist_bkts =
        _get_max_hist_bkts(HIST_BKTS_TYPES 0UL) + 1; // +1 for upper bound bucket
#undef X

#define X(name, ...) hist_bucket_boundaries_t name;
    HIST_BKTS_TYPES
#undef X

    HistogramBuckets() {
#define X(name, ...) name = {__VA_ARGS__};
        HIST_BKTS_TYPES
#undef X
    }
};

} // namespace sisl
#endif // METRICS_HISTOGRAM_BUCKETS_HPP
