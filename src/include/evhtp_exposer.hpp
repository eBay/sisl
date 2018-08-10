#ifndef MONITOR_EVHTP_EXPOSER_H_
#define MONITOR_EVHTP_EXPOSER_H_

#include "metrics_monitor.hpp"
#include "prometheus/registry.h"

#include <evhtp.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <memory>
#include <vector>

namespace monitor {

class MetricsHandler;
  
class EvhtpExposer {
 public:
  EvhtpExposer();
          
  ~EvhtpExposer(); 

  void RegisterCollectable(const std::weak_ptr<prometheus::Collectable>& collectable);

  // A singleton object to expose the metrics
  // We can create different instances that corresponding to different logics (for example,
  // critical metrics or not) 
  static EvhtpExposer&  Instance() {
    static EvhtpExposer uinstance;
    return uinstance;
    
  }
  
  // Follow the event handler format defined in evhtp
  static void EvhtpCommandHandler (evhtp_request_t *req, void *task);
  // Retrieve metrics in the specified format 
  std::unique_ptr<MetricsResult> GetMetricsReport ();

  void RegisterCollectionCallback (MetricsCollectionCallback func);
  MetricsCollectionCallback GetCollectionCallback();
  
 private:
  std::vector<std::weak_ptr<prometheus::Collectable>> collectables_;
  std::shared_ptr<prometheus::Registry> exposer_registry_;
  std::unique_ptr<MetricsHandler> metrics_handler_;

  MetricsCollectionCallback metrics_collection_callback_;

 };
}


#endif // MONITOR_EVHTP_EXPOSER_H_
