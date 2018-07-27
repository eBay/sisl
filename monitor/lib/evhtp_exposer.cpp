#include <chrono>
#include <string>
#include <thread>

#include "evhtp_exposer.hpp"
#include "evhtp_handler.hpp"
#include "expo_format.hpp"

#include "prometheus/metrics.pb.h"

namespace monitor {

static void HttpResponseOK(evhtp_request_t* req, const char* result, size_t length, const char* content_type) {
    if (result != nullptr) {
        if (content_type)
            evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", content_type, 0, 1));
        evbuffer_add(req->buffer_out, result, length);
    } else {
        evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/plain", 0, 0));
        evbuffer_add_printf(req->buffer_out,
                            "{ \"errorCode\": 0, \"errorDetail\": \"Success. Warning: Empty result.\" }");
    }

    evhtp_send_reply(req, EVHTP_RES_OK);
}

EvhtpExposer::EvhtpExposer()
    : exposer_registry_(std::make_shared<prometheus::Registry>()),
      metrics_handler_(new MetricsHandler{collectables_, *exposer_registry_}),
      metrics_collection_callback_(nullptr) {
    RegisterCollectable(exposer_registry_);
}

EvhtpExposer::~EvhtpExposer() {}

void EvhtpExposer::RegisterCollectionCallback (MetricsCollectionCallback func) {
  metrics_collection_callback_ = func;
}

MetricsCollectionCallback EvhtpExposer::GetCollectionCallback() {
  return (metrics_collection_callback_);
}
  
void EvhtpExposer::RegisterCollectable(
    const std::weak_ptr<prometheus::Collectable>& collectable) {
  collectables_.push_back(collectable);
}


std::unique_ptr<MetricsResult> EvhtpExposer::GetMetricsReport() {
  if (metrics_collection_callback_ != nullptr) {
      //make callback to the monstor db to get the updated metrics.
      //have the metrics result only go out of scope after the handler gets the serialized result
      return (metrics_collection_callback_());
  } else {
      return nullptr;
  }
}

void EvhtpExposer::EvhtpCommandHandler (evhtp_request_t *req, void *task){
    auto metrics_result = EvhtpExposer::Instance().GetMetricsReport();
    if (metrics_result) metrics_result->publish();

    auto metrics_report = EvhtpExposer::Instance().metrics_handler_->GetHandle();
    std::string encoding =  GetContentTypeWithExpoFormat (kExpositionFormat);
    HttpResponseOK (req, metrics_report.data(), metrics_report.size(), encoding.data());
}

} // namespace monitor
