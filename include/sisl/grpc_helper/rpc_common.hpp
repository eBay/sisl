#pragma once

namespace grpc_helper {
class GrpcServer;
class GenericRpcData;
struct RPCHelper {
    static bool has_server_shutdown(const GrpcServer* server);
    static bool run_generic_handler_cb(GrpcServer* server, const std::string& method,
                                       boost::intrusive_ptr< GenericRpcData >& rpc_data);
    static grpc::Status do_authorization(const GrpcServer* server, const grpc::ServerContext* srv_ctx);
    static grpc::StatusCode to_grpc_statuscode(const sisl::AuthVerifyStatus status);
};
} // namespace grpc_helper
