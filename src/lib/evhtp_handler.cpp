#include "evhtp.h"
#include "prometheus/serializer.h"
#include "prometheus/json_serializer.h"
#include "prometheus/text_serializer.h"
#include "prometheus/protobuf_delimited_serializer.h"

#include <sds_logging/logging.h>

#include "evhtp_handler.hpp"
#include "expo_format.hpp"

namespace monitor {

// static std::string GetAcceptedEncoding(evhtp_header_t *header) {
//  if (std::string{header->key} == "Accept") {
// return std::string{header->val};
//}
// else {
//  return "";
//}
//}

MetricsHandler::MetricsHandler(const std::vector<std::weak_ptr<prometheus::Collectable>>& collectables,
                               prometheus::Registry& registry)
    : collectables_(collectables), bytes_transfered_family_(prometheus::BuildCounter()
                                                                .Name("exposer_bytes_transfered")
                                                                .Help("bytesTransferred to metrics services")
                                                                .Register(registry)),
      bytes_transfered_(bytes_transfered_family_.Add({})),
      num_scrapes_family_(prometheus::BuildCounter()
                              .Name("exposer_total_scrapes")
                              .Help("Number of times metrics were scraped")
                              .Register(registry)),
      num_scrapes_(num_scrapes_family_.Add({})),
      request_latencies_family_(prometheus::BuildHistogram()
                                    .Name("exposer_request_latencies")
                                    .Help("Latencies of serving scrape requests, in milliseconds")
                                    .Register(registry)),
      request_latencies_(request_latencies_family_.Add(
          {}, prometheus::Histogram::BucketBoundaries{
	        0.1, 0.2, 0.3, 0.5, 0.7, 1, 2, 5, 10, 20, 40, 80, 160, 320, 640, 1280, 2560})) {}

std::string MetricsHandler::GetHandle() {
    using namespace io::prometheus::client;

    auto start_time_of_request = std::chrono::steady_clock::now();
    auto metrics = CollectMetrics();

    auto serializer = std::unique_ptr<prometheus::Serializer>{};

    // Follow the protocol definition:
    // https://prometheus.io/docs/instrumenting/exposition_formats/
    switch (kExpositionFormat) {
    case ExpositionFormats::kTextFormat:
        serializer.reset(new prometheus::TextSerializer());
        break;
    case ExpositionFormats::kJsonFormat:
        serializer.reset(new prometheus::JsonSerializer());
        break;
    case ExpositionFormats::kProtoBufferFormat:
        serializer.reset(new prometheus::ProtobufDelimitedSerializer());
        break;
    default:
        serializer.reset(new prometheus::TextSerializer());
    }

    auto body = serializer->Serialize(metrics);
    if (CVLOG_IS_ON(VMODULE_METRICS, 4)) {
        CVLOG(VMODULE_METRICS, 4) << "Content:" << body.c_str();
        CVLOG(VMODULE_METRICS, 4) << "Content Length: " << static_cast<unsigned long>(body.size());
    }

    auto stop_time_of_request = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop_time_of_request - start_time_of_request);
    request_latencies_.Observe(duration.count());

    bytes_transfered_.Increment(body.size());
    num_scrapes_.Increment();

    return body;
}

std::vector<io::prometheus::client::MetricFamily> MetricsHandler::CollectMetrics() const {
    auto collected_metrics = std::vector<io::prometheus::client::MetricFamily>{};

    for (auto&& wcollectable : collectables_) {
        auto collectable = wcollectable.lock();
        if (!collectable) { continue; }

        for (auto metric : collectable->Collect()) {
            collected_metrics.push_back(metric);
        }
    }

    return collected_metrics;
}

} // namespace monitor
