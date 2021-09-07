#include <memory>
#include <string>
#include <functional>
#include <chrono>
#include <thread>
#include <mutex>

#include <sds_logging/logging.h>
#include <sds_options/options.h>

#include "grpc_helper/rpc_client.hpp"
#include "grpc_helper/rpc_server.hpp"
#include "grpc_helper_test.grpc.pb.h"

using namespace grpc_helper;
using namespace ::grpc_helper_test;
using namespace std::placeholders;

class TestClient {
public:
    static constexpr int GRPC_CALL_COUNT = 100;
    const std::string WORKER_NAME{"Worker-1"};

    void validate_echo_reply(const EchoRequest& req, EchoReply& reply, ::grpc::Status& status) {
        RELEASE_ASSERT_EQ(status.ok(), true, "echo request {} failed, status {}: {}", req.message(),
                          status.error_code(), status.error_message());
        LOGDEBUGMOD(grpc_server, "echo request {} reply {}", req.message(), reply.message());
        RELEASE_ASSERT_EQ(req.message(), reply.message());
        {
            std::unique_lock lk(m_wait_mtx);
            if (--m_echo_counter == 0) { m_cv.notify_all(); }
        }
    }

    void validate_ping_reply(const PingRequest& req, PingReply& reply, ::grpc::Status& status) {
        RELEASE_ASSERT_EQ(status.ok(), true, "ping request {} failed, status {}: {}", req.seqno(), status.error_code(),
                          status.error_message());
        LOGDEBUGMOD(grpc_server, "ping request {} reply {}", req.seqno(), reply.seqno());
        RELEASE_ASSERT_EQ(req.seqno(), reply.seqno());
        {
            std::unique_lock lk(m_wait_mtx);
            if (--m_ping_counter == 0) { m_cv.notify_all(); }
        }
    }

    void run(const std::string& server_address) {
        auto client = std::make_unique< GrpcAsyncClient >(server_address, "", "");
        client->init();
        GrpcAsyncClientWorker::create_worker(WORKER_NAME, 4);

        auto echo_stub = client->make_stub< EchoService >(WORKER_NAME);
        auto ping_stub = client->make_stub< PingService >(WORKER_NAME);

        m_ping_counter = GRPC_CALL_COUNT;
        m_echo_counter = GRPC_CALL_COUNT;
        for (int i = 1; i <= GRPC_CALL_COUNT * 2; ++i) {
            if ((i % 2) == 0) {
                if ((i % 4) == 0) {
                    EchoRequest req;
                    req.set_message(std::to_string(i));
                    echo_stub->call_unary< EchoRequest, EchoReply >(
                        req, &EchoService::StubInterface::AsyncEcho,
                        [req, this](EchoReply& reply, ::grpc::Status& status) {
                            validate_echo_reply(req, reply, status);
                        },
                        1);
                } else {
                    echo_stub->call_rpc< EchoRequest, EchoReply >(
                        [i](EchoRequest& req) { req.set_message(std::to_string(i)); },
                        &EchoService::StubInterface::AsyncEcho,
                        [this](ClientRpcData< EchoRequest, EchoReply >& cd) {
                            validate_echo_reply(cd.req(), cd.reply(), cd.status());
                        },
                        1);
                }
            } else {
                if ((i % 3) == 0) {
                    PingRequest req;
                    req.set_seqno(i);
                    ping_stub->call_unary< PingRequest, PingReply >(
                        req, &PingService::StubInterface::AsyncPing,
                        [req, this](PingReply& reply, ::grpc::Status& status) {
                            validate_ping_reply(req, reply, status);
                        },
                        1);
                } else {
                    ping_stub->call_rpc< PingRequest, PingReply >(
                        [i](PingRequest& req) { req.set_seqno(i); }, &PingService::StubInterface::AsyncPing,
                        [this](ClientRpcData< PingRequest, PingReply >& cd) {
                            validate_ping_reply(cd.req(), cd.reply(), cd.status());
                        },
                        1);
                }
            }
        }
    }

    void wait() {
        std::unique_lock lk(m_wait_mtx);
        m_cv.wait(lk, [this]() { return ((m_echo_counter == 0) && (m_ping_counter == 0)); });
        GrpcAsyncClientWorker::shutdown_all();
    }

private:
    int m_echo_counter;
    int m_ping_counter;
    std::mutex m_wait_mtx;
    std::condition_variable m_cv;
};

class TestServer {
public:
    class EchoServiceImpl {
    public:
        virtual ~EchoServiceImpl() = default;

        virtual bool echo_request(const AsyncRpcDataPtr< EchoService, EchoRequest, EchoReply >& rpc_data) {
            LOGDEBUGMOD(grpc_server, "receive echo request {}", rpc_data->request().message());
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
                    "Echo", &EchoService::AsyncService::RequestEcho,
                    std::bind(&EchoServiceImpl::echo_request, this, _1))) {
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
            LOGDEBUGMOD(grpc_server, "receive ping request {}", rpc_data->request().seqno());
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
                    "Ping", &PingService::AsyncService::RequestPing,
                    std::bind(&PingServiceImpl::ping_request, this, _1))) {
                LOGERROR("register ping rpc failed");
                return false;
            }

            return true;
        }
    };

    void start(const std::string& server_address) {
        LOGINFO("Start echo and ping server on {}...", server_address);
        m_grpc_server = GrpcServer::make(server_address, 4, "", "");
        m_echo_impl = new EchoServiceImpl();
        m_echo_impl->register_service(m_grpc_server);

        m_ping_impl = new PingServiceImpl();
        m_ping_impl->register_service(m_grpc_server);

        m_grpc_server->run();
        LOGINFO("Server listening on {}", server_address);

        m_echo_impl->register_rpcs(m_grpc_server);
        m_ping_impl->register_rpcs(m_grpc_server);
    }

    void shutdown() {
        LOGINFO("Shutting down grpc server");
        m_grpc_server->shutdown();
    }

private:
    GrpcServer* m_grpc_server = nullptr;
    EchoServiceImpl* m_echo_impl = nullptr;
    PingServiceImpl* m_ping_impl = nullptr;
};

SDS_LOGGING_INIT(logging, grpc_server)
SDS_OPTIONS_ENABLE(logging)

int main(int argc, char** argv) {
    SDS_OPTIONS_LOAD(argc, argv, logging)
    sds_logging::SetLogger("async_client");

    TestServer server;
    std::string server_address("0.0.0.0:50051");
    server.start(server_address);

    TestClient client;
    client.run(server_address);
    client.wait();

    server.shutdown();
    return 0;
}
