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

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include "grpc_helper/rpc_client.hpp"
#include "sds_grpc_test.grpc.pb.h"

using namespace ::grpc;
using namespace ::sisl;
using namespace ::sds_grpc_test;
using namespace std::placeholders;

class EchoAndPingClient : public GrpcSyncClient {

public:
    using GrpcSyncClient::GrpcSyncClient;

    virtual void init() {
        GrpcSyncClient::init();

        echo_stub_ = MakeStub< EchoService >();
        ping_stub_ = MakeStub< PingService >();
    }

    const std::unique_ptr< EchoService::StubInterface >& echo_stub() { return echo_stub_; }

    const std::unique_ptr< PingService::StubInterface >& ping_stub() { return ping_stub_; }

private:
    std::unique_ptr< EchoService::StubInterface > echo_stub_;
    std::unique_ptr< PingService::StubInterface > ping_stub_;
};

#define GRPC_CALL_COUNT 10

int RunClient(const std::string& server_address) {

    auto client = std::make_unique< EchoAndPingClient >(server_address, "", "");
    if (!client) {
        LOGERROR("Create grpc sync client failed.");
        return -1;
    }
    client->init();

    int ret = 0;
    for (int i = 0; i < GRPC_CALL_COUNT; i++) {
        ClientContext context;

        if (i % 2 == 0) {
            EchoRequest request;
            EchoReply reply;

            request.set_message(std::to_string(i));
            Status status = client->echo_stub()->Echo(&context, request, &reply);
            if (!status.ok()) {
                LOGERROR("echo request {} failed, status {}: {}", request.message(), status.error_code(),
                         status.error_message());
                continue;
            }

            LOGINFO("echo request {} reply {}", request.message(), reply.message());

            if (request.message() == reply.message()) { ret++; }
        } else {
            PingRequest request;
            PingReply reply;

            request.set_seqno(i);
            Status status = client->ping_stub()->Ping(&context, request, &reply);
            if (!status.ok()) {
                LOGERROR("ping request {} failed, status {}: {}", request.seqno(), status.error_code(),
                         status.error_message());
                continue;
            }

            LOGINFO("ping request {} reply {}", request.seqno(), reply.seqno());

            if (request.seqno() == reply.seqno()) { ret++; }
        }
    }

    return ret;
}

SISL_LOGGING_INIT()
SISL_OPTIONS_ENABLE(logging)

int main(int argc, char** argv) {
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger("sync_client");

    std::string server_address("0.0.0.0:50051");

    if (RunClient(server_address) != GRPC_CALL_COUNT) {
        LOGERROR("Only {} calls are successful", GRPC_CALL_COUNT);
        return 1;
    }

    return 0;
}
