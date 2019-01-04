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
        std::cout << "receive echo request " << request.message() << std::endl;
        response.set_message(request.message());
        return ::grpc::Status::OK;
    }

    bool register_service(GrpcServer* server) {

        if (!server->register_async_service<EchoService>()) {
            std::cout << "register service failed" << std::endl;
            return false;
        }

        return true;
    }

    bool register_rpcs(GrpcServer* server) {
        std::cout << "register rpc calls" << std::endl;
        if (!server->register_rpc<EchoService, EchoRequest, EchoReply>(
                    &EchoService::AsyncService::RequestEcho,
                    std::bind(&EchoServiceImpl::echo_request, this, _1, _2))) {
            std::cout << "register rpc failed" << std::endl;
            return false;
        }

        return true;
    }

};



class PingServiceImpl {

  public:
    virtual ~PingServiceImpl() = default;

    virtual ::grpc::Status ping_request(PingRequest& request, PingReply& response) {
        std::cout << "receive ping request " << request.seqno() << std::endl;
        response.set_seqno(request.seqno());
        return ::grpc::Status::OK;
    }

    bool register_service(GrpcServer* server) {

        if (!server->register_async_service<PingService>()) {
            std::cout << "register ping service failed" << std::endl;
            return false;
        }

        return true;
    }

    bool register_rpcs(GrpcServer* server) {
        std::cout << "register rpc calls" << std::endl;
        if (!server->register_rpc<PingService, PingRequest, PingReply>(
                    &PingService::AsyncService::RequestPing,
                    std::bind(&PingServiceImpl::ping_request, this, _1, _2))) {
            std::cout << "register ping rpc failed" << std::endl;
            return false;
        }

        return true;
    }

};


void RunServer() {

    std::string server_address("0.0.0.0:50051");

    auto server = GrpcServer::make(server_address, 4, "", "");

    EchoServiceImpl * echo_impl = new EchoServiceImpl();
    echo_impl->register_service(server);

    PingServiceImpl * ping_impl = new PingServiceImpl();
    ping_impl->register_service(server);

    server->run();
    std::cout << "Server listening on " << server_address << std::endl;

    echo_impl->register_rpcs(server);
    ping_impl->register_rpcs(server);

    while (!server->is_terminated()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    delete server;
}

SDS_LOGGING_INIT()
SDS_OPTIONS_ENABLE(logging)

int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging)
    std::cout << "Start echo server ..." << std::endl;

    RunServer();
    return 0;
}

