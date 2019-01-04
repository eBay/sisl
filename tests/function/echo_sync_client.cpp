/*
 * echo_sync_client.cpp
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



#include "sds_grpc/client.h"
#include "sds_grpc_test.grpc.pb.h"


using namespace ::grpc;
using namespace ::sds::grpc;
using namespace ::sds_grpc_test;
using namespace std::placeholders;


class EchoAndPingClient : public GrpcSyncClient {

  public:

    using GrpcSyncClient::GrpcSyncClient;

    virtual bool init() {
        if (!GrpcSyncClient::init()) {
            return false;
        }

        echo_stub_ = MakeStub<EchoService>();
        ping_stub_ = MakeStub<PingService>();

        return true;
    }

    const std::unique_ptr<EchoService::StubInterface>& echo_stub() {
        return echo_stub_;
    }

    const std::unique_ptr<PingService::StubInterface>& ping_stub() {
        return ping_stub_;
    }

  private:

    std::unique_ptr<EchoService::StubInterface> echo_stub_;
    std::unique_ptr<PingService::StubInterface> ping_stub_;

};


#define GRPC_CALL_COUNT 10

int RunClient(const std::string& server_address) {

    auto client = std::make_unique<EchoAndPingClient>(server_address, "", "");
    if (!client || !client->init()) {
        std::cout << "Create grpc sync client failed." << std::endl;
        return -1;
    }

    int ret = 0;
    for (int i = 0; i < GRPC_CALL_COUNT; i++) {
        ClientContext context;

        if (i % 2 == 0) {
            EchoRequest  request;
            EchoReply reply;

            request.set_message(std::to_string(i));
            Status status = client->echo_stub()->Echo(&context, request, &reply);
            if (!status.ok()) {
                std::cout << "echo request " << request.message() <<
                          " failed, status " << status.error_code() <<
                          ": " << status.error_message() << std::endl;
                continue;
            }

            std::cout << "echo request " << request.message() <<
                      " reply " << reply.message() << std::endl;

            if (request.message() == reply.message()) {
                ret++;
            }
        } else {
            PingRequest  request;
            PingReply reply;

            request.set_seqno(i);
            Status status = client->ping_stub()->Ping(&context, request, &reply);
            if (!status.ok()) {
                std::cout << "ping request " << request.seqno() <<
                          " failed, status " << status.error_code() <<
                          ": " << status.error_message() << std::endl;
                continue;
            }

            std::cout << "ping request " << request.seqno() <<
                      " reply " << reply.seqno() << std::endl;

            if (request.seqno() == reply.seqno()) {
                ret++;
            }
        }

    }

    return ret;
}

SDS_LOGGING_INIT()

int main(int argc, char** argv) {

    std::string server_address("0.0.0.0:50051");

    if (RunClient(server_address) != GRPC_CALL_COUNT) {
        std::cerr << "Only " << GRPC_CALL_COUNT << " calls are successful" << std::endl;
        return 1;
    }

    return 0;
}
