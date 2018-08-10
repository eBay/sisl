#pragma once

#include "http_server.hpp"
#include <admin/admin_server.hpp>
#include <common/logging.hpp>

namespace monitor {

//this class is to encapsulate the MonstorDB Admin Server that provides simple HTTP server.
//and only be used when to link with MonstorDB server codebase.
class WrappedAdminServer: public HttpServer {
 public:
   WrappedAdminServer(monstor::AdminServer* srv_thread);

   void RegisterHandler(const std::string& endpoint, HttpServerCallback handler) override;

										     
private:
    monstor::AdminServer* admin_thread_;
};

}
