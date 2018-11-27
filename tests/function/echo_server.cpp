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


};

using EchoAsyncService = ::sds_grpc_test::EchoService::AsyncService;

class EchoServer : public GrpcServer<EchoAsyncService> {

  public:
    EchoServer(EchoServiceImpl* impl)
        : GrpcServer<EchoAsyncService>(),
          impl_(impl) {
    }

    void ready() {

        std::cout << "register rpc calls" << std::endl;
        register_rpc<EchoAsyncService, EchoRequest, EchoReply>(
            &EchoAsyncService::RequestEcho,
            std::bind(&EchoServiceImpl::echo_request, impl_, _1, _2));
    }

    EchoServiceImpl* impl_;

};


void RunServer() {

    std::string server_address("0.0.0.0:50051");

    EchoServiceImpl * impl = new EchoServiceImpl();
    EchoServer* server = new EchoServer(impl);
    server->run("", "", server_address, 4);
    std::cout << "Server listening on " << server_address << std::endl;

    while (!server->is_shutdown()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    delete server;
}


int main(int arc, char* argv[]) {
    std::cout << "Start echo server ..." << std::endl;

    RunServer();
    return 0;
}

