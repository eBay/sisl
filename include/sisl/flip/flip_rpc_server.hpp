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
#pragma once

#include "proto/flip_spec.pb.h"
#include "proto/flip_server.grpc.pb.h"

namespace flip {
class FlipRPCServer final : public FlipServer::Service {
public:
    grpc::Status InjectFault(grpc::ServerContext* context, const FlipSpec* request, FlipResponse* response) override;
    grpc::Status GetFaults(grpc::ServerContext* context, const FlipNameRequest* request,
                           FlipListResponse* response) override;
    grpc::Status RemoveFault(grpc::ServerContext*, const FlipRemoveRequest* request,
                             FlipRemoveResponse* response) override;
    static void rpc_thread();
};
} // namespace flip
