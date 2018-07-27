#ifndef MONITOR_METRICS_MONITOR_H_
#define MONITOR_METRICS_MONITOR_H_

#include "histogram_buckets.hpp"
#include "prometheus/registry.h"
#include "prometheus/counter.h"
#include "prometheus/gauge.h"
#include "prometheus/histogram.h"


#include <memory>
#include <mutex>
#include <string>
#include <functional>
#include <vector>
#include <map>
#include <unordered_map>
#include <thread>

namespace monitor {

class MetricsResult {
 public:
  virtual ~MetricsResult() = default;
  virtual void publish() = 0;
};

using MetricsCollectionCallback = std::function < std::unique_ptr<MetricsResult> () >;
  
//need this class forwarding to avoid compilation errors due to the include of event-related structures
class HttpServer;
  
//a counter/gauge/histogram family can group the metrics together and only have labels like "type=put"
//and "type=get", to differentiate the concrete types. Such grouping is useful for metrics
//aggregation and multi-line visual presentation.
  
class CounterFamily {
 public:
  CounterFamily(prometheus::Family <prometheus::Counter> *family,
		const std::string& name):
    family_(family),
    name_(name) {
    
  }
  
  prometheus::Family <prometheus::Counter> *family() {
    return family_;
  }

  const std::string& name() const {
    return name_;
  }

 private:
  prometheus::Family <prometheus::Counter> *family_;
  std::string name_; //the name for the defined family
};

class GaugeFamily {
 public:
  GaugeFamily(prometheus::Family <prometheus::Gauge> *family,
	      const std::string& name):
    family_(family),
    name_(name){
  }
  
  prometheus::Family <prometheus::Gauge> *family() {
    return family_;
  }

  const std::string& name() const {
    return name_;
  }

 private:
  prometheus::Family <prometheus::Gauge> *family_;
  std::string name_; //the name for the defined family
};

class HistogramFamily {
 public:
  HistogramFamily(prometheus::Family <prometheus::Histogram> *family,
		  const std::string& name):
    family_(family),
    name_(name) {
  }
  
  prometheus::Family <prometheus::Histogram> *family() {
    return family_;
  }

  const std::string& name() const {
    return name_;
  }

 private:
  prometheus::Family <prometheus::Histogram> *family_;
  std::string name_; //the name for the defined family
};


class Counter {
 public:
  Counter (CounterFamily *family,
	   prometheus::Counter *counter,
	   const std::string& name):
    family_(family),
    counter_(counter),
    name_(name){
  }
  
  //the name of the family
  CounterFamily* family() {
    return family_;
  }

  prometheus::Counter*  counter() const  {
    return counter_;
  }

  const std::string& name() const {
    return name_;
  }

  //the wrapper to the corresponding internal Prometheus counter's function
  void Increment() {
    counter_->Increment();
  }

  //the wrapper to the corresponding internal Prometheus counter's function
  void Increment(double val) {
    counter_->Increment(val);
  }

  //the wrapper to the corresponding internal Prometheus counter's function
  double Value() const {
    return counter_->Value();
  }
  
  //update the wrapped counter's value
  void Update(double value) {
    double counter_value = counter_->Value();
    double diff = value - counter_value;
    //we rely on prometheus::counter to check whether the passed value is < 0, and if so, discard
    //the passed value
    counter_->Increment(diff);
  }


 private:
  CounterFamily* family_;
  prometheus::Counter *counter_; //the corresponding prometheus counter.
  std::string name_; //the name for the defined family
};

class Gauge {
 public:
  Gauge (GaugeFamily *family,
	 prometheus::Gauge *gauge,
  	 const std::string& name):
    family_(family),
    gauge_(gauge),
    name_(name) {
    
  }

  //the name of the family
  GaugeFamily* family() {
    return family_;
  }

  prometheus::Gauge*  gauge() const  {
    return gauge_;
  }

  const std::string& name() const {
    return name_;
  }

  //the wrapper to the corresponding internal Prometheus gauge's function
  void Increment() {
    gauge_->Increment(1.0);
  }

  //the wrapper to the corresponding internal Prometheus gauge's function
  void Increment(double value) {
    gauge_->Increment(value);
  }

  //the wrapper to the corresponding internal Prometheus gauge's function
  void Decrement() {
    gauge_->Decrement();
  }

  //the wrapper to the corresponding internal Prometheus gauge's function
  void Decrement(double value) {
    gauge_->Decrement();
  }

  //the wrapper to the corresponding internal Prometheus gauge's function
  void Set(double value) {
    gauge_->Set(value);
  }

  //the wrapper to the corresponding internal Prometheus gauge's function
  void SetToCurrentTime() {
    gauge_->SetToCurrentTime();
  }

  //the wrapper to the corresponding internal Prometheus gauge's function
  double Value() const {
    return gauge_->Value();
  }

 
  //update the wrapped gauge value;
  void Update(double value) {
    gauge_->Set(value);
  }
  
 private:
  GaugeFamily* family_;
  prometheus::Gauge *gauge_; //the corresponding prometheus gauge.
  std::string name_; //the name for the defined family
};

class Histogram {
 public:

  Histogram(HistogramFamily *family,
	   prometheus::Histogram *histogram,
           const std::string& name):
    family_(family),
    histogram_(histogram),
    name_(name) {
    
  }

  //the name of the family
  HistogramFamily *family() {
    return family_;
  }

  prometheus::Histogram*  histogram() const  {
    return histogram_;
  }
  
  const std::string& name() const {
    return name_;
  }

  //the wrapper to the corresponding internal Prometheus histogram's function
  void Observe(double value) {
    histogram_->Observe(value);
  }
  
  //update the  wrapped histogram by updating both the bucket_values and the corresponding sum.
  void Update(std::vector<double> bucket_values, double sum) {
    //we have the prometheus::histogram class get patched to support the following method.
    histogram_->TransferBucketCounters(bucket_values, sum);
  }

 private:
  HistogramFamily *family_;
  prometheus::Histogram *histogram_; //the corresponding prometheus histogram.
  std::string name_; //the name for the defined family
};


class MetricsMonitor {
 public:
  MetricsMonitor ();
  
  ~MetricsMonitor ();

  // for critical metrics
  static const std::string kPrometheusEndpoint1;
  // for non-critical metrics plus critical metrics
  static const std::string kPrometheusEndpoint2;

  // return a singleton instance of this class
  static MetricsMonitor& Instance();

  // register to HttpServer thread.
  void RegisterHttpServer(HttpServer* thread);

  // launch example threads that produces randomly generated metrics
  void RunMetricsGenerator();

  // terminate generator hreads that produces randomly generated metrics
  int StopMetricsGenerator();
  
  std::shared_ptr<prometheus::Registry>& GetRegistry() {
    return registry_;
  }

  //name and help are always with the metrics family. only labels are different for metrics
  //belong to the same family
  CounterFamily *RegisterCounterFamily (const std::string& name, const std::string& help);
  GaugeFamily *RegisterGaugeFamily (const std::string& name, const std::string& help);
  HistogramFamily *RegisterHistogramFamily (const std::string& name, const std::string& help);

  //if a counter does not have additional labels, then we register the counter family with
  //the given "name " and "help" and then add a counter with emtpy labels.
  Counter* RegisterCounter (const std::string& name, const std::string& help);

  //the family is named with "name" and "help". The counter belongs to the family with
  //its own additional labels.
  Counter* RegisterCounter (const std::string& name, const std::string& help,
			    const std::map<std::string, std::string>& labels);
  
  //the counter from an existing counter family, along with its own labels.
  Counter* RegisterCounter (CounterFamily* family,
			    const std::map<std::string, std::string>& labels);

  //similar to what is defined for counters.
  Gauge* RegisterGauge (const std::string& name, const std::string& help);
  //similar to what is defined for counters.
  Gauge* RegisterGauge (const std::string& name, const std::string& help,
			const std::map<std::string, std::string>& labels);
  //similar to what is defined for counters.
  Gauge* RegisterGauge (GaugeFamily* family,
			const std::map<std::string, std::string>& labels);

  //similar to what is defined for counters. A histogram can have its own bucket definition,
  //if not to follow the default bucket definition.
  Histogram* RegisterHistogram (const std::string& name, const std::string& help,
				const prometheus::Histogram::BucketBoundaries& buckets =
				  HistogramBuckets::DefaultBuckets);
  //similar to what is defined for counters. A histogram can have its own bucket definition,
  //if not to follow the default bucket definition.
  Histogram* RegisterHistogram (const std::string& name, const std::string& help,
				const std::map<std::string, std::string>& labels,
				const prometheus::Histogram::BucketBoundaries& buckets =
				  HistogramBuckets::DefaultBuckets);
  //similar to what is defined for counters. A histogram can have its own bucket definition,
  //if not to follow the default bucket definition.
  Histogram* RegisterHistogram (HistogramFamily* family,
			        const std::map<std::string, std::string>& labels,
				const prometheus::Histogram::BucketBoundaries& buckets =
				  HistogramBuckets::DefaultBuckets);

  //callback function to have MonstorDB to update the registered metrics, before the HTTP handler to
  //collect the metrics.
  void RegisterCollectionCallback (MetricsCollectionCallback func);
  MetricsCollectionCallback GetCollectionCallback();

  //for testing purpose
  CounterFamily* RetrieveCounterFamily(const std::string& name);
  GaugeFamily* RetrieveGaugeFamily(const std::string& name);
  HistogramFamily* RetrieveHistogramFamily(const std::string& name);

  Counter* RetrieveCounter(const std::string& name);
  Gauge* RetrieveGauge(const std::string& name);
  Histogram* RetrieveHistogram(const std::string& name);

  
 private:
  std::shared_ptr<prometheus::Registry> registry_;

  //note that for the counter/gauge/histogram family associated with emtpy name. it will have
  //to be found through the actual counters/gauges/histograms 
  std::unordered_map <std::string, CounterFamily*> counter_families_;
  std::unordered_map <std::string, GaugeFamily*> gauge_families_;
  std::unordered_map <std::string, HistogramFamily*> histogram_families_;

  //the actual counters/gauges/histograms. 
  std::unordered_map <std::string, Counter*> counters_;
  std::unordered_map <std::string, Gauge*> gauges_;
  std::unordered_map <std::string, Histogram*> histograms_;

  //to guard the access of the map-related objects
  std::recursive_mutex mutex_;

  //the callback function
  MetricsCollectionCallback metrics_collection_callback_;
  
};

}


#endif // MONITOR_METRICS_MONITOR_H_
