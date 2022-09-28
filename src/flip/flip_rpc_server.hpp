//
// Created by Kadayam, Hari on Jun 12 2019.
//
#ifndef FLIP_FLIP_RCP_SERVER_HPP
#define FLIP_FLIP_RCP_SERVER_HPP

#include "flip_spec.pb.h"
#include "flip_server.grpc.pb.h"

namespace flip {
class FlipRPCServer final : public FlipServer::Service {
public:
    grpc::Status InjectFault(grpc::ServerContext* context, const FlipSpec* request, FlipResponse* response) override;
    grpc::Status GetFaults(grpc::ServerContext* context, const FlipNameRequest* request, 
                                      FlipListResponse* response) override;
    static void rpc_thread();
};
} // namespace flip
#endif