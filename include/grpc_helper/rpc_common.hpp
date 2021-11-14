#pragma once

namespace grpc_helper {
class GrpcServer;
struct RPCHelper {
    static bool has_server_shutdown(const GrpcServer* server);

    static grpc::StatusCode to_grpc_statuscode(const sisl::AuthVerifyStatus status) {
        grpc::StatusCode ret;
        switch (status) {
        case sisl::AuthVerifyStatus::OK:
            ret = grpc::StatusCode::OK;
            break;
        case sisl::AuthVerifyStatus::UNAUTH:
            ret = grpc::StatusCode::UNAUTHENTICATED;
            break;
        case sisl::AuthVerifyStatus::FORBIDDEN:
            ret = grpc::StatusCode::PERMISSION_DENIED;
            break;
        default:
            ret = grpc::StatusCode::UNKNOWN;
            break;
        }
        return ret;
    }
};
} // namespace grpc_helper
