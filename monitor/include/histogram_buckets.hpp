#ifndef HISTOGRAM_BUCKETS_H_
#define HISTOGRAM_BUCKETS_H_

#include "prometheus/histogram.h"

namespace monitor {

//to define the histogram buckets used for various metrics
class HistogramBuckets {
public:
    static const prometheus::Histogram::BucketBoundaries DefaultBuckets;
};
}
#endif //HISTOGRAM_BUCKETS_H_
