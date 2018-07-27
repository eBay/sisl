#include "metrics_monitor.hpp"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <string>

TEST (MetricsDefinitions, createAndRetrieveHistogramsWithFamily) {
  monitor::HistogramFamily *histogramFamily=
  monitor:: MetricsMonitor::Instance()
    .RegisterHistogramFamily("monstor_db_dbcommand_latency",
 	                 "the histogram on latency of db command issued to backend db store");
  EXPECT_TRUE (histogramFamily != nullptr);

  monitor::Histogram* latencyHistogram =
    monitor::MetricsMonitor::Instance().RegisterHistogram (histogramFamily, {{"type", "read"}});
  EXPECT_TRUE (latencyHistogram != nullptr);
  
  //check by retrieving something from the prometheus family
  std::string familyName = histogramFamily->name();
  std::string histogramName= latencyHistogram->name();
  EXPECT_EQ(familyName, "monstor_db_dbcommand_latency");
  EXPECT_EQ(histogramName, "monstor_db_dbcommand_latency:type:read");

  latencyHistogram->Observe(330);
  latencyHistogram->Observe(410);
  prometheus::Histogram* realHistogram = latencyHistogram->histogram();
  realHistogram->Observe(320);
  realHistogram->Observe(400);
  
  auto metric = realHistogram->Collect();
  ASSERT_TRUE(metric.has_histogram());
  auto h = metric.histogram();
  //buckets are defined in histogram_buckets.cpp
  EXPECT_EQ(h.bucket(0).cumulative_count(), 0);
  EXPECT_EQ(h.bucket(1).cumulative_count(), 4);
  
  monitor::HistogramFamily *retrievedHistogramFamily =
    monitor::MetricsMonitor::Instance().RetrieveHistogramFamily(familyName);  
  monitor::Histogram *retrievedHistogram =
    monitor::MetricsMonitor::Instance().RetrieveHistogram(histogramName);

  EXPECT_EQ (histogramFamily, retrievedHistogramFamily);
  EXPECT_EQ (latencyHistogram, retrievedHistogram);
}


TEST (MetricsDefinitions, createAndRetrieveHistogramsWithoutFamily) {
  monitor::Histogram* histogram2 =
    monitor::MetricsMonitor::Instance().RegisterHistogram ("monstor_db_dbcommand_latency_2",
							"the histogram on latency of db command issued to backend db store");
  EXPECT_TRUE (histogram2 != nullptr);

  monitor::HistogramFamily *histogramFamily =histogram2->family();
  EXPECT_TRUE (histogramFamily != nullptr);

  
  std::string familyName = histogramFamily->name();
  std::string histogramName = histogram2->name();

  //family name and gauge name are identical.
  EXPECT_EQ(familyName, "monstor_db_dbcommand_latency_2");
  EXPECT_EQ(histogramName, "monstor_db_dbcommand_latency_2");

  histogram2->Observe(330);
  histogram2->Observe(410);
  
  prometheus::Histogram* realHistogram =histogram2->histogram();
  realHistogram->Observe(320);
  realHistogram->Observe(400);
  auto metric = realHistogram->Collect();
  ASSERT_TRUE(metric.has_histogram());
  auto h = metric.histogram();
  //buckets are defined in histogram_buckets.cpp
  EXPECT_EQ(h.bucket(0).cumulative_count(), 0);
  EXPECT_EQ(h.bucket(1).cumulative_count(), 4);

  monitor::HistogramFamily *retrievedHistogramFamily =
    monitor::MetricsMonitor::Instance().RetrieveHistogramFamily(familyName);  
  monitor::Histogram *retrievedHistogram =
    monitor::MetricsMonitor::Instance().RetrieveHistogram(histogramName);

  EXPECT_EQ (histogramFamily, retrievedHistogramFamily);
  EXPECT_EQ (retrievedHistogram, histogram2);
}


TEST (MetricsDefinitions, createAndRetrieveHistogramsWithoutFamilyButWithLabels) {
  monitor::Histogram* histogram3 =
    monitor::MetricsMonitor::Instance().RegisterHistogram ("monstor_db_dbcommand_latency_3",
							 "the histogram on latency of db command issued to backend db store",
							 {{"colo", "slc"}, {"app", "monstordb"}});
  EXPECT_TRUE (histogram3 != nullptr);

  monitor::HistogramFamily *histogram3Family = histogram3->family();
  EXPECT_TRUE (histogram3Family != nullptr);

  
  std::string familyName = histogram3Family->name();
  std::string histogramName =histogram3->name();

  //family name is the name supplied by the call
  EXPECT_EQ(familyName, "monstor_db_dbcommand_latency_3");
  //the counter name is the family name with all of the labels added. Since it is the map, the
  //order of the retrieval can not be predicted.
  EXPECT_TRUE( (histogramName == "monstor_db_dbcommand_latency_3:colo:slc:app:monstordb")
	       || (histogramName == "monstor_db_dbcommand_latency_3:app:monstordb:colo:slc"));

  
  histogram3->Observe(330);
  histogram3->Observe(410);
  
  prometheus::Histogram* realHistogram = histogram3 ->histogram();
  realHistogram->Observe(320);
  realHistogram->Observe(400);
  auto metric = realHistogram->Collect();
  ASSERT_TRUE(metric.has_histogram());
  auto h = metric.histogram();
  //buckets are defined in histogram_buckets.cpp
  EXPECT_EQ(h.bucket(0).cumulative_count(), 0);
  EXPECT_EQ(h.bucket(1).cumulative_count(), 4);

  
  monitor::HistogramFamily *retrievedHistogram3Family =
    monitor::MetricsMonitor::Instance().RetrieveHistogramFamily(familyName);  
  monitor::Histogram *retrievedHistogram3 =
    monitor::MetricsMonitor::Instance().RetrieveHistogram(histogramName);

  EXPECT_EQ (histogram3Family, retrievedHistogram3Family);
  EXPECT_EQ (histogram3, retrievedHistogram3);
}


TEST (MetricsDefinitions, updateHistogram) {
  monitor::Histogram* histogram4 =
    monitor::MetricsMonitor::Instance().RegisterHistogram ("monstor_db_dbcommand_latency_4",
							 "the histogram on latency of db command issued to backend db store",
							 {{"colo", "slc"}, {"app", "monstordb"}});
  EXPECT_TRUE (histogram4 != nullptr);

  prometheus::Histogram* realHistogram = histogram4 ->histogram();

  {
    std::vector<double> buckets_update_1=
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
    double sum_update_1 = 120;
  
    histogram4->Update(buckets_update_1, sum_update_1);
  
    auto metric = realHistogram->Collect();
    ASSERT_TRUE(metric.has_histogram());
    auto h = metric.histogram();
    //buckets are defined in histogram_buckets.cpp
    //NOTE: the following is about cummulative count, not the original buckets.
    EXPECT_EQ(h.bucket(0).cumulative_count(), 1);
    EXPECT_EQ(h.bucket(1).cumulative_count(), 3); //1+2
  }

  {
    //the transfer of the buckets will just replace the old buckets.
    std::vector<double> buckets_update_2=
      {4, 8, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
    double sum_update_2 = 120;
  
    histogram4->Update(buckets_update_2, sum_update_2);

    auto metric = realHistogram->Collect();
    ASSERT_TRUE(metric.has_histogram());
    auto h = metric.histogram();
    //buckets are defined in histogram_buckets.cpp
    //NOTE: the following is about cummulative count, not the original buckets.
    EXPECT_EQ(h.bucket(0).cumulative_count(), 4);
    EXPECT_EQ(h.bucket(1).cumulative_count(), 12);//4+8

  }
  
}


// Call RUN_ALL_TESTS() in main()
int main (int argc, char ** argv) {
  //with main, we can attach some google test related hooks.
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler() ;

  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}


