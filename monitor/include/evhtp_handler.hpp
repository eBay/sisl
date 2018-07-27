#ifndef MONITOR_METRICS_HANDLER_H_
#define MONITOR_METRICS_HANDLER_H_

#include <memory>
#include <vector>

#include "prometheus/registry.h"

namespace monitor {

class MetricsHandler {
  public:
    MetricsHandler(const std::vector<std::weak_ptr<prometheus::Collectable>>& collectables,
                   prometheus::Registry& registry);

    ~MetricsHandler() {}

    std::string GetHandle();

  private:
    std::vector<io::prometheus::client::MetricFamily> CollectMetrics() const;

    const std::vector<std::weak_ptr<prometheus::Collectable>>& collectables_;
    prometheus::Family<prometheus::Counter>& bytes_transfered_family_;
    prometheus::Counter& bytes_transfered_;
    prometheus::Family<prometheus::Counter>& num_scrapes_family_;
    prometheus::Counter& num_scrapes_;
    prometheus::Family<prometheus::Histogram>& request_latencies_family_;
    prometheus::Histogram& request_latencies_;
};

} // namespace monitor

#endif // MONITOR_METRICS_HANDLER_H_
