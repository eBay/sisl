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



class EchoSyncClient : public GrpcConnection<EchoService> {

  public:
    EchoSyncClient(const std::string& server_addr, uint32_t dead_line,
                   ::grpc::CompletionQueue* cq,
                   const std::string& target_domain,
                   const std::string& ssl_cert)
        : GrpcConnection<EchoService>(server_addr, dead_line, cq, target_domain, ssl_cert) {
    }

};


#define GRPC_CALL_COUNT 10


int RunClient(const std::string& server_address) {

    GrpcClient* fix_this_name = new GrpcClient();

    auto client = GrpcConnectionFactory::Make<EchoSyncClient>(
                      server_address, 5, &(fix_this_name->cq()), "", "");
    if (!client) {
        std::cout << "Create echo client failed." << std::endl;
        return -1;
    }

    int ret = 0;

    for (int i = 0; i < GRPC_CALL_COUNT; i++) {
        ClientContext context;
        EchoRequest  request;
        EchoReply reply;

        request.set_message(std::to_string(i));

        Status status = client->stub()->Echo(&context, request, &reply);
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

    }

    return ret;
}

int main(int argc, char** argv) {

    std::string server_address("0.0.0.0:50051");

    if (RunClient(server_address) != GRPC_CALL_COUNT) {
        std::cerr << "Only " << GRPC_CALL_COUNT << " calls are successful" << std::endl;
        return 1;
    }

    return 0;
}
