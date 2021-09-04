#pragma once

namespace grpc_helper {
class GrpcServer;
struct RPCHelper {
    static bool has_server_shutdown(const GrpcServer* server);
};
} // namespace grpc_helper
