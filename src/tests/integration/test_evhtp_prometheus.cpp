#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <math.h>

#include "evhtp_server.hpp"
#include "evhtp_exposer.hpp"
#include "evhtp_handler.hpp"

#include "metrics_monitor.hpp"
#include "common/logging.hpp"

#define PI 3.1415926

using namespace monitor;

void increment_counterfunction_1(Counter* c){
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    c->Increment();
    LOG(INFO) << "counter: " << c->Value();
  }
}

void increment_gaugefunction_1(Gauge* g){
  static size_t time_counter = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    time_counter++;
    double result = sin (time_counter*PI/180);
    g->Set(result);
    LOG(INFO) << "Gauge 1: " << g->Value();
  }
}

void increment_gaugefunction_2 (Gauge* g){
  static size_t time_counter = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    time_counter++;
    double result = sin (time_counter*PI/2.0/180);
    g->Set(result);
    LOG(INFO) << "Gauge 2: " << g->Value();
  }
}

// invoke the program  ./test_evhtp_prometheus 10.254.40.173
int main (int argc, char ** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <ip address bound>" << std::endl;
    return 1;
  }

  std::cout << argv[0] << "specifies ip address: " << argv[1] << std::endl;

  //EvhtpServer httpserver ("127.0.0.1", 8080);
  EvhtpServer httpserver (argv[1], 8080);
  const std::string prometheus_endpoint = "/metrics";

  // Lazily start the metrics monitor and http server registration.
  MetricsMonitor::Instance().RegisterHttpServer(&httpserver);

  // Add a new counter family to the registry (faimilies combine values with
  // the same name, but distinct lable dimensions
  CounterFamily* counter_family = MetricsMonitor::Instance().RegisterCounterFamily(
  				         "nudata_time_running_seconds",
					 "seconds elapsed since this server running");

  // Add a counter to a metric family
  Counter* second_counter = MetricsMonitor::Instance().RegisterCounter(counter_family,
			       {{"keyspace", "seller"}, {"partition_id", "42"}});

  //Add a new gauge family
  GaugeFamily* gauge_family = MetricsMonitor::Instance().RegisterGaugeFamily(
 			        "nudata_cpu_consumption_seconds",
			        "cpu consumption since this server running");

  // Add a gauge to the metric family
  Gauge* first_gauge = MetricsMonitor::Instance().RegisterGauge(gauge_family,
        {{"keyspace", "seller"}, {"partition_id", "48"}});

  // Add a second gauge to the metric family
  Gauge* second_gauge = MetricsMonitor::Instance().RegisterGauge(gauge_family,
        {{"keyspace", "listing"}, {"partition_id", "72"}});

  // Launch a thread, so that we can increase the counter
  std::thread counter_thread {increment_counterfunction_1, second_counter};

  // Launch a  thread, so that we can adjust the gauge
  std::thread gauge_thread_1 {increment_gaugefunction_1, first_gauge};
  // Launch a second thread, so that we can adjust the gauge
  std::thread gauge_thread_2 {increment_gaugefunction_2, second_gauge};

  // Calling start become a blocking call.
  httpserver.start();
}
