#include "evhtp_server.hpp"
#include "evhtp_exposer.hpp"
#include "evhtp_handler.hpp"

#include "metrics_monitor.hpp"
#include "dbtxnprocessing_metrics.hpp"
#include "dbconnectionthread_metrics.hpp"
#include "common/logging.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <random>
#include <thread>
#include <iostream>
#include <cmath>

using namespace monitor;
using namespace prometheus;

void update_connection_thread_metrics(DbConnectionThreadMetrics& dbthread_metrics){
  // Seed with a real random value, if available
  std::random_device r;
  // Choose a random mean between 1 and 6
  std::default_random_engine e1(r());
  std::uniform_int_distribution<int> uniform_dist(1, 10);
  //int mean = uniform_dist(e1);

  for (;;) {
    int wait_queue_length = 0;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    dbthread_metrics.grpc_memory_pressure->Set(uniform_dist(e1));
    dbthread_metrics.mongo_active_connections->Set(uniform_dist(e1));
    dbthread_metrics.worker_threads_number->Set(uniform_dist(e1));
    dbthread_metrics.active_task_counts->Set(uniform_dist(e1));
    dbthread_metrics.wait_queue_length->Set((wait_queue_length=uniform_dist(e1)));

    LOG(INFO) << "wait queue length: " << wait_queue_length;
  }
}

void update_dbtxnprocessing_metrics  (DbTxnProcessingMetrics& dbtxn){

  // Seed with a real random value, if available
  std::random_device r;

  // Choose a random mean between 1 and 6
  std::default_random_engine e1(r());
  std::uniform_int_distribution<int> uniform_dist(1, 6);
  int mean = uniform_dist(e1);

  // Generate a normal distribution around that mean
  std::seed_seq seed2{r(), r(), r(), r(), r(), r(), r(), r()};
  std::mt19937 e2(seed2);
  std::normal_distribution<> normal_dist(mean, 2);

  std::map<int, int> hist;

  int total_commits  = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    dbtxn.expected_total_commits->Increment((total_commits=normal_dist(e2)));
    dbtxn.docs_processed_number->Increment(normal_dist(e2));
    dbtxn.txn_document_size->Set(normal_dist(e2));
    dbtxn.total_actual_commits->Set(normal_dist(e2));

    LOG(INFO) << "number of total_commits since last time: " << total_commits;
  }
}

// invoke the program  ./test_detailed_metrics 10.254.40.173
int main (int argc, char ** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <ip address bound>" << std::endl;
    return 1;
  }

  std::cout << argv[0] << "specifies ip address: " << argv[1] << std::endl;
  
  // Create a http server running on port 8080
  // EvhtpServer httpserver ("127.0.0.1", 8080);
  EvhtpServer httpserver (argv[1], 8080);

  // Lazily start the metrics monitor and http server registration.
  MetricsMonitor::Instance().RegisterHttpServer(&httpserver);
  
  // Launch thread to update db connection thread related metrics
  DbConnectionThreadMetrics dbconnection_metrics;
  std::thread thread_1 {update_connection_thread_metrics, std::ref(dbconnection_metrics)};

  // Launch thread to update db background flushing
  DbTxnProcessingMetrics dbtxnprocessing_metrics; 
  std::thread thread_2 {update_dbtxnprocessing_metrics, std::ref(dbtxnprocessing_metrics)};

  // Calling start become a blocking call.
  httpserver.start();
}
