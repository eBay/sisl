
#include <common/logging.hpp>
#include "evhtp_server.hpp"

namespace monitor {

EvhtpServer::EvhtpServer(const std::string& ip, int port)
    : started_(false), ipaddress_(ip), portnum_(port), server_context_({nullptr, nullptr}) {}

void EvhtpServer::start () {
  server_context_.evbase = event_base_new();
  if (server_context_.evbase != nullptr) {
    server_context_.htp = evhtp_new (server_context_.evbase, NULL);

    if (server_context_.htp != nullptr) {
      struct timeval timeout = {.tv_sec = 5000, .tv_usec = 0};
      evhtp_set_timeouts(server_context_.htp, &timeout, &timeout);

      // Register all of the call bach handlers
      for (std::vector<std::pair<std::string, HttpServerCallback>>::iterator
        it = handlers_.begin(); it != handlers_.end(); ++it) {
	//evhtp_callback_cb *handler = (*it).second.target<evhtp_callback_cb>() ;
        //evhtp_set_cb (server_context_.htp, (*it).first.c_str(), (*it).second, nullptr);
	evhtp_set_cb (server_context_.htp, (*it).first.c_str(), (*it).second, nullptr);
      }

     if (evhtp_bind_socket(server_context_.htp, ipaddress_.c_str(), portnum_, 1024) == 0) {
        started_ = true;
        LOG(INFO) << "http server started at port " << portnum_;

        event_base_loop(server_context_.evbase, 0);  // blocking call for this thread
     }
     else {
       LOG(ERROR) << "http server fails to start at ip address: " << ipaddress_
	   << " port: " << portnum_;
       // Free the http resources
       evhtp_free(server_context_.htp);
       server_context_.htp = nullptr;
       event_base_free(server_context_.evbase);
       server_context_.evbase = nullptr;

       }
     }
     else {
       LOG(ERROR) << "httpserver context (evhtp_t) cannot be created successfully";
       // Free the created HTTP resources
       event_base_free(server_context_.evbase);
       server_context_.evbase = nullptr;
    }
   }
   else {
     LOG(ERROR) << "httpserver context (evbase_t) cannot be created successfully";
   }
}

void EvhtpServer::RegisterHandler (const std::string&  uri, HttpServerCallback handler) {
  std::pair <std::string, HttpServerCallback> pair;
  pair = std::make_pair (uri, handler);
  handlers_.push_back (pair);
}

void EvhtpServer::close() {
    if (started_) {
        if (server_context_.htp != nullptr) {
            evhtp_unbind_socket(server_context_.htp);
            evhtp_free(server_context_.htp);
        }

        if (server_context_.evbase != nullptr) { event_base_free(server_context_.evbase); }

        LOG(INFO) << "http server shutdown at ip address: " << ipaddress_ << "port: " << portnum_;
    }
}

EvhtpServer::~EvhtpServer() { close(); }

} // namespace monitor
