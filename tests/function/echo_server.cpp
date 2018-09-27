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


class RequestDispatcher {

public:
    virtual ~RequestDispatcher() = default;

    virtual EchoReply echo_request(EchoRequest& request) {
        EchoReply reply;

        reply.set_message(request.message());

        std::cout << "receive echo request " << request.message() << std::endl;

        return reply;
    }
};


class EchoServer : public GrpcServer<Echo::AsyncService> {

public:
    EchoServer(RequestDispatcher* dispatcher)
        : GrpcServer<sds_grpc_test::Echo::AsyncService>(),
          dispatcher_(dispatcher) {
    }

    void ready() {

        (new ServerCallData<Echo::AsyncService, EchoRequest, EchoReply>
        (&service_, completion_queue_.get(), "echo",
                &Echo::AsyncService::RequestEcho,
                std::bind(&RequestDispatcher::echo_request, dispatcher_, _1)))->proceed();
    }

    void process(ServerCallMethod * cm) {
        cm->proceed();
    }

    RequestDispatcher* dispatcher_;

};


void RunServer() {

    std::string server_address("0.0.0.0:50051");

    RequestDispatcher * dispatcher = new RequestDispatcher();
    EchoServer* server = new EchoServer(dispatcher);
    server->run("", "", server_address, 4);
    std::cout << "Server listening on " << server_address << std::endl;

    while (!server->is_shutdown())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

}


int main(int arc, char* argv[])
{
    std::cout << "Start echo server ..." << std::endl;

    RunServer();
    return 0;
}

