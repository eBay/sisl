#ifndef MONITOR_EVHTP_SERVER_H_
#define MONITOR_EVHTP_SERVER_H_

#include "http_server.hpp"
#include <evhtp.h>

#include <string>
#include <vector>
#include <utility>

namespace monitor {

//the local standalone http server for unit/integration testing. When the library linked
//with the MonstorDB server code, it will use the Admin Server thread that supports HttpServer
//interface instead.
class EvhtpServer: public HttpServer {

 struct EvhtpThreadContext {
   evbase_t *evbase;
   evhtp_t  *htp;
 };
 
 public:
  EvhtpServer(const std::string& ip,  int port);

  ~EvhtpServer();

  void start ();
  void RegisterHandler (const std::string&  endpoint, HttpServerCallback handler) override;

  void close();
  

 private:
  bool started_;
  std::string ipaddress_;
  int portnum_;
  std::vector<std::pair <std::string, HttpServerCallback> > handlers_;
  EvhtpThreadContext  server_context_; 
};
} // namespace monitor

#endif // MONITOR_EVHTP_SERVER_H_
