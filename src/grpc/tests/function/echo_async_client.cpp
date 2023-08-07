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
#include <memory>
#include <string>
#include <functional>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <mutex>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include "sisl/grpc/rpc_client.hpp"
#include "sisl/grpc/rpc_server.hpp"
#include "sisl/grpc/generic_service.hpp"
#include "grpc_helper_test.grpc.pb.h"

using namespace sisl;
using namespace ::grpc_helper_test;
using namespace std::placeholders;

struct DataMessage {
    int m_seqno;
    std::string m_buf;

    DataMessage() = default;
    DataMessage(const int n, const std::string& buf) : m_seqno{n}, m_buf{buf} {}

    void SerializeToString(std::string& str_buf) const {
        // first char denotes number of digits in seq_no
        str_buf.append(std::to_string(numDigits(m_seqno)));
        // append the seqno
        str_buf.append(std::to_string(m_seqno));
        // append the data buffer
        str_buf.append(m_buf);
    }
    void DeserializeFromString(const std::string& str_buf) {
        int num_dig = str_buf[0] - '0';
        m_seqno = std::stoi(str_buf.substr(1, num_dig));
        m_buf = str_buf.substr(1 + num_dig);
    }

    static int numDigits(int n) {
        int ret = 0;
        for (; n > 0; ret++) {
            n /= 10;
        }
        return ret;
    }
};

static void DeserializeFromByteBuffer(const grpc::ByteBuffer& buffer, DataMessage& msg) {
    std::vector< grpc::Slice > slices;
    (void)buffer.Dump(&slices);
    std::string buf;
    buf.reserve(buffer.Length());
    for (auto s = slices.begin(); s != slices.end(); s++) {
        buf.append(reinterpret_cast< const char* >(s->begin()), s->size());
    }
    msg.DeserializeFromString(buf);
}
static void SerializeToByteBuffer(grpc::ByteBuffer& buffer, const DataMessage& msg) {
    std::string buf;
    msg.SerializeToString(buf);
    buffer.Clear();
    grpc::Slice slice(buf);
    grpc::ByteBuffer tmp(&slice, 1);
    buffer.Swap(&tmp);
}

static const std::string GENERIC_CLIENT_MESSAGE{"I am a super client!"};
static const std::string GENERIC_METHOD{"SendData"};

class TestClient {
public:
    static constexpr int GRPC_CALL_COUNT = 400;
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

    void validate_generic_reply(const DataMessage& req, grpc::ByteBuffer& reply, ::grpc::Status& status) {
        RELEASE_ASSERT_EQ(status.ok(), true, "generic request {} failed, status {}: {}", req.m_seqno,
                          status.error_code(), status.error_message());
        DataMessage svr_msg;
        DeserializeFromByteBuffer(reply, svr_msg);
        RELEASE_ASSERT_EQ(req.m_seqno, svr_msg.m_seqno);
        RELEASE_ASSERT_EQ(req.m_buf, svr_msg.m_buf);
        {
            std::unique_lock lk(m_wait_mtx);
            if (--m_generic_counter == 0) { m_cv.notify_all(); }
        }
    }

    void run(const std::string& server_address) {
        auto client = std::make_unique< GrpcAsyncClient >(server_address, "", "");
        client->init();
        GrpcAsyncClientWorker::create_worker(WORKER_NAME, 4);

        auto echo_stub = client->make_stub< EchoService >(WORKER_NAME);
        auto ping_stub = client->make_stub< PingService >(WORKER_NAME);
        auto generic_stub = client->make_generic_stub(WORKER_NAME);

        m_echo_counter = static_cast< int >(GRPC_CALL_COUNT / 2);
        // all numbers divisible by 3 but not 2
        m_ping_counter = static_cast< int >((GRPC_CALL_COUNT - 3) / 6) + 1;
        m_generic_counter = GRPC_CALL_COUNT - m_echo_counter - m_ping_counter;

        for (int i = 1; i <= GRPC_CALL_COUNT; ++i) {
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
            } else if ((i % 3) == 0) {
                // divide all numbers divisible by 3 and not by 2 into two equal buckets
                if ((((i + 3) / 6) % 2) == 0) {
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
            } else {
                // divide all numbers not divisible by 2 and 3 into two equal buckets
                if (((i + 1) % 6) == 0) {
                    DataMessage req(i, GENERIC_CLIENT_MESSAGE);
                    grpc::ByteBuffer cli_buf;
                    SerializeToByteBuffer(cli_buf, req);
                    generic_stub->call_unary(
                        cli_buf, GENERIC_METHOD,
                        [req, this](grpc::ByteBuffer& reply, ::grpc::Status& status) {
                            validate_generic_reply(req, reply, status);
                        },
                        1);
                } else {
                    DataMessage data_msg(i, GENERIC_CLIENT_MESSAGE);
                    generic_stub->call_rpc([data_msg](grpc::ByteBuffer& req) { SerializeToByteBuffer(req, data_msg); },
                                           GENERIC_METHOD,
                                           [data_msg, this](GenericClientRpcData& cd) {
                                               validate_generic_reply(data_msg, cd.reply(), cd.status());
                                           },
                                           1);
                }
            }
        }
    }

    void wait() {
        std::unique_lock lk(m_wait_mtx);
        m_cv.wait(lk,
                  [this]() { return ((m_echo_counter == 0) && (m_ping_counter == 0) && (m_generic_counter == 0)); });
        GrpcAsyncClientWorker::shutdown_all();
    }

private:
    int m_echo_counter;
    int m_ping_counter;
    int m_generic_counter;
    std::mutex m_wait_mtx;
    std::condition_variable m_cv;
};

class TestServer {
public:
    class EchoServiceImpl final {
        std::atomic< uint32_t > num_calls = 0ul;

    public:
        ~EchoServiceImpl() = default;

        void register_service(GrpcServer* server) {
            auto const res = server->register_async_service< EchoService >();
            RELEASE_ASSERT(res, "Failed to Register Service");
        }

        void register_rpcs(GrpcServer* server) {
            LOGINFO("register rpc calls");
            auto const res = server->register_rpc< EchoService, EchoRequest, EchoReply, false >(
                "Echo", &EchoService::AsyncService::RequestEcho,
                [this](const AsyncRpcDataPtr< EchoService, EchoRequest, EchoReply >& rpc_data) {
                    if ((++num_calls % 2) == 0) {
                        LOGDEBUGMOD(grpc_server, "respond async echo request {}", rpc_data->request().message());
                        std::thread([rpc = rpc_data] {
                            rpc->response().set_message(rpc->request().message());
                            rpc->send_response();
                        }).detach();
                        return false;
                    }
                    LOGDEBUGMOD(grpc_server, "respond sync echo request {}", rpc_data->request().message());
                    rpc_data->response().set_message(rpc_data->request().message());
                    return true;
                });
            RELEASE_ASSERT(res, "register rpc failed");
        }
    };

    class PingServiceImpl final {
        std::atomic< uint32_t > num_calls = 0ul;

    public:
        ~PingServiceImpl() = default;

        void register_service(GrpcServer* server) {
            auto const res = server->register_async_service< PingService >();
            RELEASE_ASSERT(res, "Failed to Register Service");
        }

        void register_rpcs(GrpcServer* server) {
            LOGINFO("register rpc calls");
            auto const res = server->register_rpc< PingService, PingRequest, PingReply, false >(
                "Ping", &PingService::AsyncService::RequestPing,
                [this](const AsyncRpcDataPtr< PingService, PingRequest, PingReply >& rpc_data) {
                    if ((++num_calls % 2) == 0) {
                        LOGDEBUGMOD(grpc_server, "respond async ping request {}", rpc_data->request().seqno());
                        std::thread([rpc = rpc_data] {
                            rpc->response().set_seqno(rpc->request().seqno());
                            rpc->send_response();
                        }).detach();
                        return false;
                    }
                    LOGDEBUGMOD(grpc_server, "respond sync ping request {}", rpc_data->request().seqno());
                    rpc_data->response().set_seqno(rpc_data->request().seqno());
                    return true;
                });
            RELEASE_ASSERT(res, "register ping rpc failed");
        }
    };

    class GenericServiceImpl final {
        std::atomic< uint32_t > num_calls = 0ul;
        std::atomic< uint32_t > num_completions = 0ul;

        static void set_response(const grpc::ByteBuffer& req, grpc::ByteBuffer& resp) {
            DataMessage cli_request;
            DeserializeFromByteBuffer(req, cli_request);
            RELEASE_ASSERT((cli_request.m_buf == GENERIC_CLIENT_MESSAGE), "Could not parse response buffer");
            SerializeToByteBuffer(resp, cli_request);
        }

    public:
        void register_service(GrpcServer* server) {
            auto const res = server->register_async_generic_service();
            RELEASE_ASSERT(res, "Failed to Register Service");
        }

        void register_rpcs(GrpcServer* server) {
            LOGINFO("register rpc calls");
            auto const res =
                server->register_generic_rpc(GENERIC_METHOD, [this](boost::intrusive_ptr< GenericRpcData >& rpc_data) {
                    rpc_data->set_comp_cb([this](boost::intrusive_ptr< GenericRpcData >&) { num_completions++; });
                    if ((++num_calls % 2) == 0) {
                        LOGDEBUGMOD(grpc_server, "respond async generic request, call_num {}", num_calls.load());
                        std::thread([this, rpc = rpc_data] {
                            set_response(rpc->request(), rpc->response());
                            rpc->send_response();
                        }).detach();
                        return false;
                    }
                    set_response(rpc_data->request(), rpc_data->response());
                    return true;
                });
            RELEASE_ASSERT(res, "register generic rpc failed");
        }

        bool compare_counters() {
            if (num_calls != num_completions) {
                LOGERROR("num calls: {}, num_completions = {}", num_calls.load(), num_completions.load());
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

        m_generic_impl = new GenericServiceImpl();
        m_generic_impl->register_service(m_grpc_server);

        m_grpc_server->run();
        LOGINFO("Server listening on {}", server_address);

        m_echo_impl->register_rpcs(m_grpc_server);
        m_ping_impl->register_rpcs(m_grpc_server);
        m_generic_impl->register_rpcs(m_grpc_server);
    }

    void shutdown() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        RELEASE_ASSERT(m_generic_impl->compare_counters(), "num calls and num completions do not match!");
        LOGINFO("Shutting down grpc server");
        m_grpc_server->shutdown();
        delete m_grpc_server;
        delete m_echo_impl;
        delete m_ping_impl;
        delete m_generic_impl;
    }

private:
    GrpcServer* m_grpc_server = nullptr;
    EchoServiceImpl* m_echo_impl = nullptr;
    PingServiceImpl* m_ping_impl = nullptr;
    GenericServiceImpl* m_generic_impl = nullptr;
};

SISL_LOGGING_INIT(logging, grpc_server)
SISL_OPTIONS_ENABLE(logging)

int main(int argc, char** argv) {
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger("async_client");

    TestServer server;
    std::string server_address("0.0.0.0:50052");
    server.start(server_address);

    TestClient client;
    client.run(server_address);
    client.wait();

    server.shutdown();
    return 0;
}
