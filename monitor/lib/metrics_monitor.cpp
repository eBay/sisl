#include <utility>
#include <common/logging.hpp>
#include "metrics_monitor.hpp"
#include "evhtp_exposer.hpp"
#include "http_server.hpp"
#include <glog/logging.h>

namespace monitor {

// Designed for critical metrics
const std::string MetricsMonitor::kPrometheusEndpoint1 = "/metrics";
// Designed for non-critical metrics plus critical metrics
const std::string MetricsMonitor::kPrometheusEndpoint2 = "/metrics2";
// Specifiy the HTTP response to Prometheus Server


MetricsMonitor::MetricsMonitor()
  : registry_(std::make_shared<prometheus::Registry>()),
    metrics_collection_callback_(nullptr) {
}

MetricsMonitor& MetricsMonitor::Instance() {
    static MetricsMonitor uinstance;
    return uinstance;
}

void MetricsMonitor::RegisterHttpServer(HttpServer* server) {
  // Do registration on the HTTP Server
  server->RegisterHandler(MetricsMonitor::kPrometheusEndpoint1,
			         EvhtpExposer::EvhtpCommandHandler);
  // Also register the exposer with the registry
  EvhtpExposer::Instance().RegisterCollectable(registry_);
}
  
MetricsMonitor::~MetricsMonitor() {
  for ( unsigned i = 0; i < counter_families_.bucket_count(); ++i) {
    for ( auto local_it = counter_families_.begin(i);
          local_it!= counter_families_.end(i); ++local_it ) {
          if (local_it->second) {
            delete local_it->second;
          }
     }
  }

  counter_families_.clear();


  for ( unsigned i = 0; i < gauge_families_.bucket_count(); ++i) {
    for ( auto local_it = gauge_families_.begin(i);
          local_it!= gauge_families_.end(i); ++local_it ) {
          if (local_it->second) {
            delete local_it->second;
          }
     }
  }

  gauge_families_.clear();


  for ( unsigned i = 0; i < histogram_families_.bucket_count(); ++i) {
    for ( auto local_it = histogram_families_.begin(i);
          local_it!= histogram_families_.end(i); ++local_it ) {
          if (local_it->second) {
            delete local_it->second;
          }
     }
  }

  histogram_families_.clear();

  for ( unsigned i = 0; i < counters_.bucket_count(); ++i) {
    for ( auto local_it = counters_.begin(i);
          local_it!= counters_.end(i); ++local_it ) {
          if (local_it->second) {
            delete local_it->second;
          }
     }
  }

  counters_.clear();

  for ( unsigned i = 0; i < gauges_.bucket_count(); ++i) {
    for ( auto local_it = gauges_.begin(i);
          local_it!= gauges_.end(i); ++local_it ) {
          if (local_it->second) {
            delete local_it->second;
          }
     }
  }

  gauges_.clear();


  for ( unsigned i = 0; i < histograms_.bucket_count(); ++i) {
    for ( auto local_it = histograms_.begin(i);
          local_it!= histograms_.end(i); ++local_it ) {
          if (local_it->second) {
            delete local_it->second;
          }
     }
  }
  
  histograms_.clear();
}

CounterFamily* MetricsMonitor::RegisterCounterFamily (
	            const std::string& name, const std::string& help){
  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map<std::string, CounterFamily*>::const_iterator got = counter_families_.find(name);

  if (got == counter_families_.end()) {
     prometheus::Family<prometheus::Counter>& family =
       prometheus::BuildCounter().Name (name).Help (help).Register (*registry_);
     CounterFamily* counter_family  = new CounterFamily(&family, name);
     counter_families_.insert (std::make_pair (name, counter_family));
     return counter_family;
   }
   else {
     return got->second;
   }
}

GaugeFamily* MetricsMonitor::RegisterGaugeFamily (const std::string& name, const std::string& help){

  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map<std::string, GaugeFamily*>::const_iterator got = gauge_families_.find(name);

  if (got == gauge_families_.end()) {
    prometheus::Family<prometheus::Gauge>& family =
       prometheus::BuildGauge().Name (name).Help (help).Register (*registry_);
    GaugeFamily* gauge_family = new GaugeFamily(&family, name);
    gauge_families_.insert (std::make_pair (name, gauge_family));
    return gauge_family;
  }
  else {
    return got->second;
  }
}

HistogramFamily* MetricsMonitor::RegisterHistogramFamily (const std::string& name,
							  const std::string& help){
  std::lock_guard<std::recursive_mutex> g {mutex_};
  std::unordered_map<std::string, HistogramFamily*>::const_iterator got = histogram_families_.find(name);

  if (got == histogram_families_.end()) {
    prometheus::Family <prometheus::Histogram>& family =
       prometheus::BuildHistogram().Name (name).Help (help).Register (*registry_);
    HistogramFamily* histogram_family =  new HistogramFamily (&family, name);
    histogram_families_.insert (std::make_pair (name, histogram_family));
    return histogram_family;
  }
  else {
     return got->second;
  }
}

//neeed to create a family using the name, and then create the counter with the empty name 
Counter* MetricsMonitor::RegisterCounter (const std::string& name, const std::string& help){
  std::lock_guard<std::recursive_mutex> g {mutex_};
  std::unordered_map<std::string, Counter*>::const_iterator got = counters_.find(name);
  
  if (got == counters_.end()) {
    // not found, need to create it.
    CounterFamily *family = RegisterCounterFamily (name, help);
    if (family != nullptr) {
      prometheus::Counter& counter =  family->family()->Add ({});

      Counter* wrapped_counter = new Counter (family, &counter, name);
      counters_.insert (std::make_pair (name, wrapped_counter));
      
      return wrapped_counter; 
    }
    else {
      LOG(ERROR) << "failed to register counter family: " << name; 
      return nullptr;
    }
  }
  else {
    return got->second; 
  }
}

//the name belong to the family; the labels belong to the counter
Counter* MetricsMonitor::RegisterCounter (const std::string& name, const std::string& help,
					  const std::map<std::string, std::string>& labels) {
  std::string fullname (name);
  auto it = labels.begin();
  while(it != labels.end()) {
    fullname += ":";
    fullname += it->first;
    fullname += ":";
    fullname += it->second;
    ++it;
  }

  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map<std::string, Counter*>::const_iterator got = counters_.find(fullname);

  if (got == counters_.end()) {
    CounterFamily *family = RegisterCounterFamily (name, help);
    if (family != nullptr) {
      prometheus::Counter& counter =  family->family()->Add (labels);
     
      Counter* wrapped_counter = new Counter (family, &counter, fullname);
      counters_.insert (std::make_pair (fullname, wrapped_counter));
      
      return wrapped_counter; 
    }
    else {
      LOG(ERROR) << "failed to register counter family: " << name; 
      return nullptr;
    }
     
  }
  else {
    return got->second;
  }
  
}


Counter* MetricsMonitor::RegisterCounter (CounterFamily* family,
					  const std::map<std::string, std::string>& labels) {
  if (family == nullptr) {
    LOG(ERROR) << "counter family passed-in is null";
    return nullptr; 
  }
  
  //the name starts with the family's namepe
  std::string fullname (family->name());
  auto it = labels.begin();
  while(it != labels.end()) {
    fullname += ":";
    fullname += it->first;
    fullname += ":";
    fullname += it->second;
    ++it;
  }

  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map<std::string, Counter*>::const_iterator got = counters_.find(fullname);

  if (got == counters_.end()) {
      prometheus::Counter& counter =  family->family()->Add (labels);
     
      Counter* wrapped_counter = new Counter (family, (prometheus::Counter*)&counter, fullname);
      counters_.insert (std::make_pair (fullname, wrapped_counter));
      
      return wrapped_counter; 
  }
  else {
    return got->second;
  }
  
}

//neeed to create a family using the name, and then create the gauge with the empty name 
Gauge* MetricsMonitor::RegisterGauge (const std::string& name, const std::string& help){
  std::lock_guard<std::recursive_mutex> g {mutex_};

  std::unordered_map<std::string, Gauge*>::const_iterator got = gauges_.find(name);

  if (got == gauges_.end()) {
    // not found, need to create it.
    GaugeFamily *family = RegisterGaugeFamily (name, help);
    if (family != nullptr) {
      prometheus::Gauge& gauge =  family->family()->Add ({});
     
      Gauge* wrapped_gauge = new Gauge (family, &gauge, name);
      gauges_.insert (std::make_pair (name, wrapped_gauge));
      
      return wrapped_gauge; 
    }
    else {
      LOG(ERROR) << "failed to register gauge family: " << name; 
      return nullptr;
    }
  }
  else {
    return got->second; 
  }
}

//the name belong to the family; the labels belong to the counter
Gauge* MetricsMonitor::RegisterGauge (const std::string& name, const std::string& help,
				      const std::map<std::string, std::string>& labels) {
  Gauge* result = nullptr;
  
  std::string fullname (name);
  auto it = labels.begin();
  while(it != labels.end()) {
    fullname += ":";
    fullname += it->first;
    fullname += ":";
    fullname += it->second;
    ++it;
  }

  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map<std::string, Gauge*>::const_iterator got = gauges_.find(fullname);

  if (got == gauges_.end()) {
    GaugeFamily *family = RegisterGaugeFamily (name, help);
    if (family != nullptr) {
      prometheus::Gauge& gauge =  family->family()->Add (labels);
     
      Gauge* wrapped_gauge = new Gauge (family, &gauge, fullname);
      gauges_.insert (std::make_pair (fullname, wrapped_gauge));
      
      result = wrapped_gauge;
    }
    else {
      LOG(ERROR) << "failed to register gauge family: " << name; 
    }
  }
  else {
    result = got->second;
  }

  return result;
}


Gauge* MetricsMonitor::RegisterGauge (GaugeFamily* family,
				      const std::map<std::string, std::string>& labels) {
  if (family == nullptr) {
    LOG(ERROR) << "Gauge family passed-in is null";
    return nullptr; 
  }
  
  //the name starts with the family's namepe
  std::string fullname (family->name());
  auto it = labels.begin();
  while(it != labels.end()) {
    fullname += ":";
    fullname += it->first;
    fullname += ":";
    fullname += it->second;
    ++it;
  }

  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map<std::string, Gauge*>::const_iterator got = gauges_.find(fullname);

  if (got == gauges_.end()) {
    prometheus::Gauge & gauge =  family->family()->Add (labels);
     
    Gauge* wrapped_gauge = new Gauge (family, &gauge, fullname);
    gauges_.insert (std::make_pair (fullname, wrapped_gauge));
      
    return wrapped_gauge; 
  }
  else {
    return got->second;
  }
}

//==histogram

//neeed to create a family using the name, and then create the histogram that inherits the name 
Histogram* MetricsMonitor::RegisterHistogram (const std::string& name, const std::string& help,
					      const prometheus::Histogram::BucketBoundaries& buckets){
  std::lock_guard<std::recursive_mutex> g {mutex_};
 
  std::unordered_map<std::string, Histogram*>::const_iterator got = histograms_.find(name);

  if (got == histograms_.end()) {
    // not found, need to create it.
    HistogramFamily *family = RegisterHistogramFamily (name, help);
    if (family != nullptr) {
      prometheus::Histogram& histogram =  family->family()->Add ({}, buckets);
     
      Histogram* wrapped_histogram = new Histogram (family, &histogram, name);
      histograms_.insert (std::make_pair (name, wrapped_histogram));
      
      return wrapped_histogram; 
    }
    else {
      LOG(ERROR) << "failed to register histogram family: " << name; 
      return nullptr;
    }
  }
  else {
    return got->second; 
  }
}

//the name belongs to the family and is inherited by the histogram; the labels belong to the histogram
Histogram* MetricsMonitor::RegisterHistogram (const std::string& name, const std::string& help,
					      const std::map<std::string, std::string>& labels,
					      const prometheus::Histogram::BucketBoundaries& buckets) {
  std::string fullname (name);
  auto it = labels.begin();
  while(it != labels.end()) {
    fullname += ":";
    fullname += it->first;
    fullname += ":";
    fullname += it->second;
    ++it;
  }

  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map<std::string, Histogram*>::const_iterator got = histograms_.find(fullname);

  if (got == histograms_.end()) {
    HistogramFamily *family = RegisterHistogramFamily (name, help);
    if (family != nullptr) {
      prometheus::Histogram& histogram =  family->family()->Add (labels, buckets);
     
      Histogram* wrapped_histogram = new Histogram (family, &histogram, fullname);
      histograms_.insert (std::make_pair (fullname, wrapped_histogram));
      
      return wrapped_histogram; 
    }
    else {
      LOG(ERROR) << "failed to register histogram family: " << name; 
      return nullptr;
    }
     
  }
  else {
    return got->second;
  }
  
}


Histogram* MetricsMonitor::RegisterHistogram (HistogramFamily* family,
			       const std::map<std::string, std::string>& labels,
			       const prometheus::Histogram::BucketBoundaries& buckets) {
  if (family == nullptr) {
    LOG(ERROR) << "Histogram family passed-in is null";
    return nullptr; 
  }
  
  //the name starts with the family's namepe
  std::string fullname (family->name());
  auto it = labels.begin();
  while(it != labels.end()) {
    fullname += ":";
    fullname += it->first;
    fullname += ":";
    fullname += it->second;
    ++it;
  }

  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map<std::string, Histogram*>::const_iterator got = histograms_.find(fullname);

  if (got == histograms_.end()) {
    prometheus::Histogram & histogram =  family->family()->Add (labels, buckets);
     
    Histogram* wrapped_histogram = new Histogram (family, &histogram, fullname);
    histograms_.insert (std::make_pair (fullname, wrapped_histogram));
      
    return wrapped_histogram; 
  }
  else {
    return got->second;
  }
  
}

//for testing purpose
CounterFamily* MetricsMonitor::RetrieveCounterFamily(const std::string&name){
  std::lock_guard<std::recursive_mutex> g {mutex_};

  std::unordered_map <std::string, CounterFamily*>::const_iterator got =
    counter_families_.find(name);

  if (got != counter_families_.end()){
    return got->second; 
  }
  else {
    return nullptr;
  }
}
  
GaugeFamily* MetricsMonitor::RetrieveGaugeFamily(const std::string&name){
  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map <std::string, GaugeFamily*>::const_iterator got =
    gauge_families_.find(name);

  if (got != gauge_families_.end()){
    return got->second; 
  }
  else {
    return nullptr;
  }
}

HistogramFamily* MetricsMonitor::RetrieveHistogramFamily(const std::string&name) {
  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map <std::string, HistogramFamily*>::const_iterator got =
  histogram_families_.find(name);

  if (got != histogram_families_.end()){
    return got->second; 
  }
  else {
    return nullptr;
  }
}

Counter* MetricsMonitor::RetrieveCounter(const std::string&name){
  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map <std::string, Counter*>::const_iterator got = counters_.find(name);

  if (got != counters_.end()){
    return got->second; 
  }
  else {
    return nullptr;
  }
}
  
Gauge* MetricsMonitor::RetrieveGauge(const std::string& name) {
  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map <std::string, Gauge*>::const_iterator got = gauges_.find(name);

  if (got != gauges_.end()){
    return got->second; 
  }
  else {
    return nullptr;
  }
}

Histogram* MetricsMonitor::RetrieveHistogram(const std::string&name) {
  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  std::unordered_map <std::string, Histogram*>::const_iterator got = histograms_.find(name);

  if (got != histograms_.end()){
    return got->second; 
  }
  else {
    return nullptr;
  }
}

void MetricsMonitor::RegisterCollectionCallback (MetricsCollectionCallback func) {
  std::lock_guard<std::recursive_mutex> g {mutex_};
  
  metrics_collection_callback_ = func;
  //propagate down to http exposer for callback.
  EvhtpExposer::Instance().RegisterCollectionCallback (func);
  
}

MetricsCollectionCallback MetricsMonitor::GetCollectionCallback() {
  std::lock_guard<std::recursive_mutex> g {mutex_};
  return ( metrics_collection_callback_); 
}

} // namespace monitor
