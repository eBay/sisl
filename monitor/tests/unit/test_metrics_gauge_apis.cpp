#include "metrics_monitor.hpp"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <string>

TEST (MetricsDefinitions, createAndRetrieveGaugesWithFamily) {
  monitor::GaugeFamily *gaugeFamily=
  monitor:: MetricsMonitor::Instance()
    .RegisterGaugeFamily("monstor_db_is_secondary",
 	                 "the guage on whether the monstordb is working as secondary replica");
  EXPECT_TRUE (gaugeFamily != nullptr);

  //to specify that this is a regular secondary that can server as read, not arbiter or hidden node.
  monitor::Gauge* masterdbIsSecondaryGauge =
    monitor::MetricsMonitor::Instance().RegisterGauge (gaugeFamily, {{"type", "regular"}});
  EXPECT_TRUE (masterdbIsSecondaryGauge != nullptr);
  
  //check by retrieving something from the prometheus family
  std::string familyName = gaugeFamily->name();
  std::string gaugeName = masterdbIsSecondaryGauge->name();
  EXPECT_EQ(familyName, "monstor_db_is_secondary");
  EXPECT_EQ(gaugeName, "monstor_db_is_secondary:type:regular");

  masterdbIsSecondaryGauge->Set(1.0);
  EXPECT_EQ(masterdbIsSecondaryGauge->Value(), 1.0);
  
  prometheus::Gauge* realGauge = masterdbIsSecondaryGauge->gauge();
  realGauge->Increment(4);
  EXPECT_EQ(realGauge->Value(), 5.0); 

  monitor::GaugeFamily *retrievedGaugeFamily =
    monitor::MetricsMonitor::Instance().RetrieveGaugeFamily(familyName);  
  monitor::Gauge *retrievedGauge =
    monitor::MetricsMonitor::Instance().RetrieveGauge(gaugeName);

  EXPECT_EQ (gaugeFamily, retrievedGaugeFamily);
  EXPECT_EQ (masterdbIsSecondaryGauge, retrievedGauge);
}


TEST (MetricsDefinitions, createAndRetrieveGaugesWithoutFamily) {
  monitor::Gauge* masterdbIsSecondaryGauge2 =
    monitor::MetricsMonitor::Instance().RegisterGauge ("monstor_db_is_secondary_2",
							 "the guage on whether the monstordb is working as secondary replica");
  EXPECT_TRUE (masterdbIsSecondaryGauge2 != nullptr);

  monitor::GaugeFamily *gaugeFamily = masterdbIsSecondaryGauge2->family();
  EXPECT_TRUE (gaugeFamily != nullptr);

  
  std::string familyName = gaugeFamily->name();
  std::string gaugeName = masterdbIsSecondaryGauge2->name();

  //family name and gauge name are identical.
  EXPECT_EQ(familyName, "monstor_db_is_secondary_2");
  EXPECT_EQ(gaugeName, "monstor_db_is_secondary_2");

  masterdbIsSecondaryGauge2->Set(1.0);
  EXPECT_EQ(masterdbIsSecondaryGauge2->Value(), 1.0);
  
  prometheus::Gauge* realGauge = masterdbIsSecondaryGauge2->gauge();
  realGauge->Increment(4);
  EXPECT_EQ(realGauge->Value(), 5.0); 

  monitor::GaugeFamily *retrievedGaugeFamily =
    monitor::MetricsMonitor::Instance().RetrieveGaugeFamily(familyName);  
  monitor::Gauge *retrievedGauge =
    monitor::MetricsMonitor::Instance().RetrieveGauge(gaugeName);

  EXPECT_EQ (gaugeFamily, retrievedGaugeFamily);
  EXPECT_EQ (retrievedGauge, masterdbIsSecondaryGauge2);
}


TEST (MetricsDefinitions, createAndRetrieveGaugesWithoutFamilyButWithLabels) {
  monitor::Gauge* masterdbIsSecondaryGauge3 =
    monitor::MetricsMonitor::Instance().RegisterGauge ("monstor_db_is_secondary_3",
							 "the guage on whether the monstordb is working as secondary replicas",
							 {{"colo", "slc"}, {"app", "monstordb"}});
  EXPECT_TRUE (masterdbIsSecondaryGauge3 != nullptr);

  monitor::GaugeFamily *gauge3Family = masterdbIsSecondaryGauge3->family();
  EXPECT_TRUE (gauge3Family != nullptr);

  
  std::string familyName = gauge3Family->name();
  std::string gaugeName =masterdbIsSecondaryGauge3->name();

  //family name is the name supplied by the call
  EXPECT_EQ(familyName, "monstor_db_is_secondary_3");
  //the counter name is the family name with all of the labels added. Since it is the map, the
  //order of the retrieval can not be predicted.
  EXPECT_TRUE( (gaugeName == "monstor_db_is_secondary_3:colo:slc:app:monstordb")
	       || (gaugeName == "monstor_db_is_secondary_3:app:monstordb:colo:slc"));

  masterdbIsSecondaryGauge3->Set(1.0);
  EXPECT_EQ(masterdbIsSecondaryGauge3->Value(), 1.0);

  prometheus::Gauge* realGauge3 = masterdbIsSecondaryGauge3 ->gauge();
  realGauge3->Increment(4);
  EXPECT_EQ(realGauge3->Value(), 5.0); 

  monitor::GaugeFamily *retrievedGauge3Family =
    monitor::MetricsMonitor::Instance().RetrieveGaugeFamily(familyName);  
  monitor::Gauge *retrievedGauge3 =
    monitor::MetricsMonitor::Instance().RetrieveGauge(gaugeName);

  EXPECT_EQ (gauge3Family, retrievedGauge3Family);
  EXPECT_EQ (masterdbIsSecondaryGauge3, retrievedGauge3);
}



TEST (MetricsDefinitions, updateGauge) {
  monitor::Gauge* masterdbIsSecondaryGauge4 =
    monitor::MetricsMonitor::Instance().RegisterGauge ("monstor_db_is_secondary_4",
							 "the guage on whether the monstordb is working as secondary replicas",
							 {{"colo", "slc"}, {"app", "monstordb"}});
  EXPECT_TRUE (masterdbIsSecondaryGauge4 != nullptr);

  prometheus::Gauge* realGauge4 = masterdbIsSecondaryGauge4 ->gauge();

  masterdbIsSecondaryGauge4->Update(4);
  EXPECT_EQ(realGauge4->Value(), 4.0);

  masterdbIsSecondaryGauge4->Update(12);
  EXPECT_EQ(realGauge4->Value(), 12.0); 

  masterdbIsSecondaryGauge4->Set(24);
  EXPECT_EQ(realGauge4->Value(), 24.0); 
}


// Call RUN_ALL_TESTS() in main()
int main (int argc, char ** argv) {
  //with main, we can attach some google test related hooks.
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler() ;

  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}


