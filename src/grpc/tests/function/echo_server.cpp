/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
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
#include <memory>
#include <string>
#include <fstream>
#include <functional>
#include <chrono>
#include <thread>
#include <signal.h>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include "sisl/grpc/rpc_server.hpp"
#include "grpc_helper_test.grpc.pb.h"

using namespace ::grpc;
using namespace sisl;
using namespace ::grpc_helper_test;
using namespace std::placeholders;

class EchoServiceImpl {
public:
    virtual ~EchoServiceImpl() = default;

    virtual bool echo_request(const AsyncRpcDataPtr< EchoService, EchoRequest, EchoReply >& rpc_data) {
        LOGINFO("receive echo request {}", rpc_data->request().message());
        rpc_data->response().set_message(rpc_data->request().message());
        return true;
    }

    bool register_service(GrpcServer* server) {
        if (!server->register_async_service< EchoService >()) {
            LOGERROR("register service failed");
            return false;
        }

        return true;
    }

    bool register_rpcs(GrpcServer* server) {
        LOGINFO("register rpc calls");
        if (!server->register_rpc< EchoService, EchoRequest, EchoReply, false >(
                "Echo", &EchoService::AsyncService::RequestEcho, std::bind(&EchoServiceImpl::echo_request, this, _1))) {
            LOGERROR("register rpc failed");
            return false;
        }

        return true;
    }
};

class PingServiceImpl {

public:
    virtual ~PingServiceImpl() = default;

    virtual bool ping_request(const AsyncRpcDataPtr< PingService, PingRequest, PingReply >& rpc_data) {
        LOGINFO("receive ping request {}", rpc_data->request().seqno());
        rpc_data->response().set_seqno(rpc_data->request().seqno());
        return true;
    }

    bool register_service(GrpcServer* server) {
        if (!server->register_async_service< PingService >()) {
            LOGERROR("register ping service failed");
            return false;
        }
        return true;
    }

    bool register_rpcs(GrpcServer* server) {
        LOGINFO("register rpc calls");
        if (!server->register_rpc< PingService, PingRequest, PingReply, false >(
                "Ping", &PingService::AsyncService::RequestPing, std::bind(&PingServiceImpl::ping_request, this, _1))) {
            LOGERROR("register ping rpc failed");
            return false;
        }

        return true;
    }
};

GrpcServer* g_grpc_server = nullptr;
EchoServiceImpl* g_echo_impl = nullptr;
PingServiceImpl* g_ping_impl = nullptr;

void waiter_thread() {
    std::this_thread::sleep_for(std::chrono::seconds(5));

    LOGINFO("Shutting down grpc server");
    g_grpc_server->shutdown();
}

void StartServer() {

    std::string server_address("0.0.0.0:50051");

    g_grpc_server = GrpcServer::make(server_address, 4, "", "");

    g_echo_impl = new EchoServiceImpl();
    g_echo_impl->register_service(g_grpc_server);

    g_ping_impl = new PingServiceImpl();
    g_ping_impl->register_service(g_grpc_server);

    g_grpc_server->run();
    LOGINFO("Server listening on {}", server_address);

    g_echo_impl->register_rpcs(g_grpc_server);
    g_ping_impl->register_rpcs(g_grpc_server);
}

SISL_LOGGING_INIT(logging, grpc_server)
SISL_OPTIONS_ENABLE(logging)

int main(int argc, char* argv[]) {
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger("echo_server");
    LOGINFO("Start echo server ...");

    StartServer();

    auto t = std::thread(waiter_thread);
    while (!g_grpc_server->is_terminated()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    t.join();
    delete g_grpc_server;
    delete g_echo_impl;
    delete g_ping_impl;

    return 0;
}
