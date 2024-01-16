/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#include <iostream>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>

#include "sisl/flip/flip_rpc_server.hpp"
#include "sisl/flip/flip.hpp"

SISL_LOGGING_DEF(flip)

namespace flip {
grpc::Status FlipRPCServer::InjectFault(grpc::ServerContext*, const FlipSpec* request, FlipResponse* response) {
    LOGTRACEMOD(flip, "InjectFault request = {}", request->DebugString());
    flip::Flip::instance().add(*request);
    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status FlipRPCServer::GetFaults(grpc::ServerContext*, const FlipNameRequest* request,
                                      FlipListResponse* response) {
    LOGTRACEMOD(flip, "GetFaults request = {}", request->DebugString());
    auto resp = request->name().size() ? flip::Flip::instance().get(request->name()) : flip::Flip::instance().get_all();
    for (const auto& r : resp) {
        response->add_infos()->set_info(r);
    }
    LOGTRACEMOD(flip, "GetFaults response = {}", response->DebugString());
    return grpc::Status::OK;
}

grpc::Status FlipRPCServer::RemoveFault(grpc::ServerContext*, const FlipRemoveRequest* request,
                                        FlipRemoveResponse* response) {
    LOGTRACEMOD(flip, "RemoveFault request = {}", request->DebugString());
    response->set_num_removed(flip::Flip::instance().remove(request->name()));
    return grpc::Status::OK;
}

class FlipRPCServiceWrapper : public FlipRPCServer::Service {
public:
    void print_method_names() {
        for (auto i = 0; i < 2; ++i) {
            auto method = (::grpc::internal::RpcServiceMethod*)GetHandler(i);
            if (method) { LOGINFOMOD(flip, "Method name = {}", method->name()); }
        }
    }
};

void FlipRPCServer::rpc_thread() {
    std::string server_address("0.0.0.0:50051");
    FlipRPCServer service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService((FlipRPCServer::Service*)&service);
    m_server = builder.BuildAndStart();
    LOGINFOMOD(flip, "Flip GRPC Server listening on {}", server_address);
    m_server->Wait();
}

} // namespace flip
