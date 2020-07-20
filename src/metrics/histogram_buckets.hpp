//
// Created by Kadayam, Hari on 12/11/18.
//

#ifndef METRICS_HISTOGRAM_BUCKETS_HPP
#define METRICS_HISTOGRAM_BUCKETS_HPP

#include <vector>
#include <cstdint>

namespace sisl {
typedef std::vector< double > hist_bucket_boundaries_t;

/* For any new histogram buckets, define a name and its values here */
#define HIST_BKTS_TYPES                                                                                                \
    X(DefaultBuckets, 300, 450, 750, 1000, 3000, 5000, 7000, 9000, 11000, 13000, 15000, 17000, 19000, 21000, 32000,    \
      45000, 75000, 110000, 160000, 240000, 360000, 540000, 800000, 1200000, 1800000, 2700000, 4000000)                \
                                                                                                                       \
    X(ExponentialOfTwoBuckets, 1, exp2(1), exp2(2), exp2(3), exp2(4), exp2(5), exp2(6), exp2(7), exp2(8), exp2(9),     \
      exp2(10), exp2(11), exp2(12), exp2(13), exp2(14), exp2(15), exp2(16), exp2(17), exp2(18), exp2(19), exp2(20),    \
      exp2(21), exp2(22), exp2(23), exp2(24), exp2(25), exp2(26), exp2(27), exp2(28), exp2(29), exp2(30), exp2(31))    \
                                                                                                                       \
    X(LinearUpto64Buckets, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,   \
      25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,  \
      53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64)                                                                  \
                                                                                                                       \
    X(LinearUpto128Buckets, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,  \
      25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,  \
      53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80,  \
      81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106,   \
      107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128)

template < typename... V >
constexpr size_t _hist_bkt_count(__attribute__((unused)) V&&... v) {
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

constexpr int64_t exp2(int exponent) { return exponent == 0 ? 1 : 2 * exp2(exponent - 1); }

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
#define X(name, ...) name = {{__VA_ARGS__}};
        HIST_BKTS_TYPES
#undef X
    }
};

} // namespace sisl
#endif // METRICS_HISTOGRAM_BUCKETS_HPP
