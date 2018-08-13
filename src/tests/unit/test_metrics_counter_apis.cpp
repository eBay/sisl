#include "metrics_monitor.hpp"

#include <gtest/gtest.h>

#include <string>

TEST (MetricsDefinitions, createAndRetrieveCountersWithFamily) {
  monitor::CounterFamily *counterFamily=
  monitor:: MetricsMonitor::Instance().RegisterCounterFamily("monstor_db_grpc_service_requests_total",
						   "the counter for mostordb grpc service requests");
  EXPECT_TRUE (counterFamily != nullptr);

  monitor::Counter* serviceRequestCounter =
    monitor::MetricsMonitor::Instance().RegisterCounter (counterFamily, {{"type", "read"}});
  EXPECT_TRUE (serviceRequestCounter != nullptr);
  
  //check by retrieving something from the prometheus family
  //prometheus::Family<prometheus::Counter> *realFamily = counterFamily->family();
  std::string familyName = counterFamily->name();
  std::string counterName =serviceRequestCounter->name();
  EXPECT_EQ(familyName, "monstor_db_grpc_service_requests_total");
  EXPECT_EQ(counterName, "monstor_db_grpc_service_requests_total:type:read");

  serviceRequestCounter->Increment(4);
  EXPECT_EQ(serviceRequestCounter->Value(), 4.0);
  
  prometheus::Counter* realCounter = serviceRequestCounter->counter();
  realCounter->Increment(4);
  EXPECT_EQ(realCounter->Value(), 8.0); 

  monitor::CounterFamily *retrievedServiceRequestCounterFamily =
    monitor::MetricsMonitor::Instance().RetrieveCounterFamily(familyName);  
  monitor::Counter *retrievedServiceRequestCounter =
    monitor::MetricsMonitor::Instance().RetrieveCounter(counterName);

  EXPECT_EQ (counterFamily, retrievedServiceRequestCounterFamily);
  EXPECT_EQ (serviceRequestCounter, retrievedServiceRequestCounter);
}



TEST (MetricsDefinitions, createAndRetrieveCountersWithoutFamily) {
  monitor::Counter* serviceResponseCounter =
    monitor::MetricsMonitor::Instance().RegisterCounter ("monstor_db_grpc_service_responses_total",
							 "the counter for monstordb grpc service responses");
  EXPECT_TRUE (serviceResponseCounter != nullptr);

  monitor::CounterFamily *counterFamily = serviceResponseCounter->family();
  EXPECT_TRUE (counterFamily != nullptr);

  
  std::string familyName = counterFamily->name();
  std::string counterName =serviceResponseCounter->name();

  //family name and counter name are identical.
  EXPECT_EQ(familyName, "monstor_db_grpc_service_responses_total");
  EXPECT_EQ(counterName, "monstor_db_grpc_service_responses_total");

  serviceResponseCounter->Increment(4);
  EXPECT_EQ(serviceResponseCounter->Value(), 4.0);
  
  prometheus::Counter* realCounter = serviceResponseCounter->counter();
  realCounter->Increment(4);
  EXPECT_EQ(realCounter->Value(), 8.0); 

  monitor::CounterFamily *retrievedServiceResponseCounterFamily =
    monitor::MetricsMonitor::Instance().RetrieveCounterFamily(familyName);  
  monitor::Counter *retrievedServiceResponseCounter =
    monitor::MetricsMonitor::Instance().RetrieveCounter(counterName);

  EXPECT_EQ (counterFamily, retrievedServiceResponseCounterFamily);
  EXPECT_EQ (serviceResponseCounter, retrievedServiceResponseCounter);
}

TEST (MetricsDefinitions, createAndRetrieveCountersWithoutFamilyButWithLabels) {
  monitor::Counter* serviceResponse2Counter =
    monitor::MetricsMonitor::Instance().RegisterCounter ("monstor_db_grpc_service_responses2_total",
							 "the counter for monstordb grpc service responses",
							 {{"colo", "slc"}, {"app", "monstorclient"}});
  EXPECT_TRUE (serviceResponse2Counter != nullptr);

  monitor::CounterFamily *counter2Family = serviceResponse2Counter->family();
  EXPECT_TRUE (counter2Family != nullptr);

  
  std::string familyName = counter2Family->name();
  std::string counterName =serviceResponse2Counter->name();

  //family name is the name supplied by the call
  EXPECT_EQ(familyName, "monstor_db_grpc_service_responses2_total");
  //the counter name is the family name with all of the labels added. Since it is the map, the
  //order of the retrieval can not be predicted.
  EXPECT_TRUE( (counterName == "monstor_db_grpc_service_responses2_total:colo:slc:app:monstorclient")
	       || (counterName == "monstor_db_grpc_service_responses2_total:app:monstorclient:colo:slc"));

  serviceResponse2Counter->Increment(4);
  EXPECT_EQ(serviceResponse2Counter->Value(), 4.0);

  prometheus::Counter* realCounter2 = serviceResponse2Counter->counter();
  realCounter2->Increment(4);
  EXPECT_EQ(realCounter2->Value(), 8.0); 

  monitor::CounterFamily *retrievedServiceResponseCounter2Family =
    monitor::MetricsMonitor::Instance().RetrieveCounterFamily(familyName);  
  monitor::Counter *retrievedServiceResponse2Counter =
    monitor::MetricsMonitor::Instance().RetrieveCounter(counterName);

  EXPECT_EQ (counter2Family, retrievedServiceResponseCounter2Family);
  EXPECT_EQ (serviceResponse2Counter, retrievedServiceResponse2Counter);
}




TEST (MetricsDefinitions, updateCounter) {
  monitor::Counter* serviceResponse4Counter =
    monitor::MetricsMonitor::Instance().RegisterCounter ("monstor_db_grpc_service_response4_total",
							 "the counter for monstordb grpc service responses",
							 {{"colo", "slc"}, {"app", "monstorclient"}});
  EXPECT_TRUE (serviceResponse4Counter != nullptr);


  serviceResponse4Counter->Update(4); //update counter value.
  prometheus::Counter* realCounter = serviceResponse4Counter->counter();
  EXPECT_EQ(realCounter->Value(), 4.0);

  serviceResponse4Counter->Update(8); //update the counter value
  EXPECT_EQ(realCounter->Value(), 8.0); 

  serviceResponse4Counter->Increment(4);
  EXPECT_EQ(serviceResponse4Counter->Value(), 12.0);

}



// Call RUN_ALL_TESTS() in main()
int main (int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}


