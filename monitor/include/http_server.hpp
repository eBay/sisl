#ifndef MONITOR_HTTP_SERVER_H_
#define MONITOR_HTTP_SERVER_H_

#include <evhtp.h>
#include <string>
#include <functional>

namespace monitor {

typedef void (*HttpServerCallback)(evhtp_request_t*, void*);
  
class HttpServer {
 public:
  virtual ~HttpServer() {
  }

  virtual void RegisterHandler(const std::string& endpoint, HttpServerCallback func) = 0;

};
  
}


#endif // MONITOR_HTTP_SERVER_H_
