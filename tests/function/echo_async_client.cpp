/*
 * echo_async_client.cpp
 *
 *  Created on: Oct 9, 2018
 */

#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <functional>
#include <chrono>
#include <thread>
#include <atomic>
#include <cassert>


#include "sds_grpc/client.h"
#include "sds_grpc_test.grpc.pb.h"


using namespace ::grpc;
using namespace ::sds::grpc;
using namespace ::sds_grpc_test;
using namespace std::placeholders;

#define WORKER_NAME  "worker-1"


class EchoAndPingAsyncClient : GrpcAsyncClient {

  public:

    using GrpcAsyncClient::GrpcAsyncClient;

    virtual bool init() {
        if (!GrpcAsyncClient::init()) {
            return false;
        }

        echo_stub_ = make_stub<EchoService>(WORKER_NAME);
        ping_stub_ = make_stub<PingService>(WORKER_NAME);

        return true;
    }


    void Echo(const EchoRequest& request,
              std::function<void(EchoReply&, ::grpc::Status& status)> callback) {

        echo_stub_->call_unary<EchoRequest, EchoReply>(request,
                &EchoService::StubInterface::AsyncEcho,
                callback);
    }

    void Ping(const PingRequest& request,
              std::function<void(PingReply&, ::grpc::Status& status)> callback) {

        ping_stub_->call_unary<PingRequest, PingReply>(request,
                &PingService::StubInterface::AsyncPing,
                callback);
    }

    AsyncStub<EchoService>::UPtr echo_stub_;
    AsyncStub<PingService>::UPtr ping_stub_;
};


std::atomic_int g_echo_counter;
std::atomic_int g_ping_counter;

/**
 * Echo implements async response handler.
 */
class Echo {
  public:

    Echo(int seqno) {
        request_.set_message(std::to_string(seqno));
    }

    void handle_echo_reply(EchoReply& reply, ::grpc::Status& status) {
        if (!status.ok()) {
            LOGERROR("echo request {} failed, status {}: {}",
                     request_.message(),
                     status.error_code(),
                     status.error_message());
            return;
        }

        LOGINFO("echo request {} reply {}", request_.message(), reply.message());

        assert(request_.message() == reply.message());
        g_echo_counter.fetch_add(1, std::memory_order_relaxed);
    }

    EchoRequest request_;
};


#define GRPC_CALL_COUNT 10

int RunClient(const std::string& server_address) {

    GrpcAyncClientWorker::create_worker(WORKER_NAME, 4);

    auto client = GrpcAsyncClient::make<EchoAndPingAsyncClient>(server_address, "", "");
    if (!client) {
        LOGCRITICAL("Create async client failed.");
        return -1;
    }

    for (int i = 0; i < GRPC_CALL_COUNT; i++) {
        if (i % 2 == 0) {
            // Async response handling logic can be put in a class's member
            // function, then use a lambda to wrap it.
            Echo * echo = new Echo(i);
            client->Echo(echo->request_,
            [echo] (EchoReply& reply, ::grpc::Status& status) {
                echo->handle_echo_reply(reply, status);
                delete echo;
            });

            // std::bind() can also be used, but need to take care releasing
            // 'echo' additionally:
            // std::bind(&Echo::handle_echo_reply, echo, _1, _2);

        } else {
            PingRequest* request = new PingRequest;
            request->set_seqno(i);

            // response can be handled with lambda directly
            client->Ping(*request,
            [request] (PingReply& reply, ::grpc::Status& status) {

                if (!status.ok()) {
                    LOGERROR("ping request {} failed, status {}: {}",
                             request->seqno(),
                             status.error_code(),
                             status.error_message());
                    return;
                }

                LOGINFO("ping request {} reply {}", request->seqno(), reply.seqno());

                assert(request->seqno() == reply.seqno());
                g_ping_counter.fetch_add(1, std::memory_order_relaxed);
                delete request;
            });
        }
    }

    GrpcAyncClientWorker::shutdown_all();

    return g_echo_counter.load() + g_ping_counter.load();
}

SDS_LOGGING_INIT()
SDS_OPTIONS_ENABLE(logging)

int main(int argc, char** argv) {
    SDS_OPTIONS_LOAD(argc, argv, logging)
    sds_logging::SetLogger("async_client");
    std::string server_address("0.0.0.0:50051");

    if (RunClient(server_address) != GRPC_CALL_COUNT) {
        LOGERROR("Only {} calls are successful", GRPC_CALL_COUNT);
        return 1;
    }

    return 0;
}


