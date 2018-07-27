#include "monitor_admin_server.hpp"

namespace monitor {

WrappedAdminServer::WrappedAdminServer(monstor::AdminServer* srv_thread): admin_thread_(srv_thread) {}

void WrappedAdminServer::RegisterHandler(const std::string& endpoint, HttpServerCallback handler) {
    admin_thread_->register_handler(endpoint, handler);
}

}
