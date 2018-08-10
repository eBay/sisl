#include "histogram_buckets.hpp"
#include <math.h>

//The default histogram buckets used for all metrics across MonstorDB
const prometheus::Histogram::BucketBoundaries monitor::HistogramBuckets::DefaultBuckets =
  {
       300,450,750,1000,3000,5000,7000,9000,11000,13000,15000, 17000, 19000, 21000,
       32000, 45000, 75000, 110000, 160000, 240000, 360000,
       540000, 800000, 1200000, 1800000, 2700000, 4000000
  };

