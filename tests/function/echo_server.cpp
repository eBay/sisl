/*
 * echo_server.cpp
 *
 *  Created on: Sep 22, 2018
 */

#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <functional>
#include <chrono>
#include <thread>
#include <signal.h>

#include <sds_logging/logging.h>
#include <sds_options/options.h>

#include "sds_grpc/server.h"
#include "sds_grpc_test.grpc.pb.h"

using namespace ::grpc;
using namespace ::sds::grpc;
using namespace ::sds_grpc_test;
using namespace std::placeholders;

class EchoServiceImpl {

public:
    virtual ~EchoServiceImpl() = default;

    virtual ::grpc::Status echo_request(EchoRequest& request, EchoReply& response) {
        LOGINFO("receive echo request {}", request.message());
        response.set_message(request.message());
        return ::grpc::Status::OK;
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
        if (!server->register_rpc< EchoService, EchoRequest, EchoReply >(
                &EchoService::AsyncService::RequestEcho, std::bind(&EchoServiceImpl::echo_request, this, _1, _2))) {
            LOGERROR("register rpc failed");
            return false;
        }

        return true;
    }
};

class PingServiceImpl {

public:
    virtual ~PingServiceImpl() = default;

    virtual ::grpc::Status ping_request(PingRequest& request, PingReply& response) {
        LOGINFO("receive ping request {}", request.seqno());
        response.set_seqno(request.seqno());
        return ::grpc::Status::OK;
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
        if (!server->register_rpc< PingService, PingRequest, PingReply >(
                &PingService::AsyncService::RequestPing, std::bind(&PingServiceImpl::ping_request, this, _1, _2))) {
            LOGERROR("register ping rpc failed");
            return false;
        }

        return true;
    }
};

GrpcServer* g_grpc_server = nullptr;
EchoServiceImpl* g_echo_impl = nullptr;
PingServiceImpl* g_ping_impl = nullptr;

void sighandler(int signum, siginfo_t* info, void* ptr) {
    LOGINFO("Received signal {}", signum);

    if (signum == SIGTERM) {
        // shutdown server gracefully for check memory leak
        LOGINFO("Shutdown grpc server");
        g_grpc_server->shutdown();
    }
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

SDS_LOGGING_INIT()
SDS_OPTIONS_ENABLE(logging)

int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging)
    sds_logging::SetLogger("echo_server");
    LOGINFO("Start echo server ...");

    StartServer();

    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = sighandler;

    sigaction(SIGTERM, &act, NULL);

    while (!g_grpc_server->is_terminated()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    delete g_grpc_server;
    delete g_echo_impl;
    delete g_ping_impl;

    return 0;
}

