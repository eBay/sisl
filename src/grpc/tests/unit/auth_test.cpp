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
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <mutex>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/auth_manager/trf_client.hpp>

#include "basic_http_server.hpp"
#include "sisl/grpc/rpc_client.hpp"
#include "sisl/grpc/rpc_server.hpp"
#include "grpc_helper_test.grpc.pb.h"
#include "test_token.hpp"

SISL_LOGGING_INIT(logging, grpc_server)
SISL_OPTIONS_ENABLE(logging)

namespace sisl::grpc::testing {
using namespace sisl;
using namespace ::grpc_helper_test;
using namespace ::testing;

static const std::string grpc_server_addr{"0.0.0.0:12345"};
static const std::string trf_token_server_ip{"127.0.0.1"};
static const uint32_t trf_token_server_port{12346};
static std::string token_response;
static void set_token_response(const std::string& raw_token) {
    token_response = "{\n"
                     "  \"access_token\": \"" +
        raw_token +
        "\",\n"
        "  \"token_type\": \"Bearer\",\n"
        "  \"expires_in\": 2000,\n"
        "  \"refresh_token\": \"dummy_refresh_token\"\n"
        "}";
}
static const std::string GENERIC_METHOD{"generic_method"};

static const std::vector< std::pair< std::string, std::string > > grpc_metadata{
    {sisl::request_id_header, "req_id1"}, {"key1", "val1"}, {"key2", "val2"}};

class EchoServiceImpl final {
public:
    ~EchoServiceImpl() = default;

    bool echo_request(const AsyncRpcDataPtr< EchoService, EchoRequest, EchoReply >& rpc_data) {
        LOGDEBUG("receive echo request {}", rpc_data->request().message());
        rpc_data->response().set_message(rpc_data->request().message());
        return true;
    }

    bool echo_request_metadata(const AsyncRpcDataPtr< EchoService, EchoRequest, EchoReply >& rpc_data) {
        LOGDEBUG("receive echo request {}", rpc_data->request().message());
        auto& client_headers = rpc_data->server_context().client_metadata();
        for (auto const& [key, val] : grpc_metadata) {
            LOGINFO("metadata received, key = {}; val = {}", key, val)
            auto const& it{client_headers.find(key)};
            if (it == client_headers.end()) {
                rpc_data->set_status(::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, ::grpc::string()));
            } else if (it->second != val) {
                LOGERROR("wrong value, expected = {}, actual = {}", val, it->second)
                rpc_data->set_status(::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, ::grpc::string()));
            }
        }
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
                std::bind(&EchoServiceImpl::echo_request, this, std::placeholders::_1))) {
            LOGERROR("register rpc failed");
            return false;
        }
        if (!server->register_rpc< EchoService, EchoRequest, EchoReply, false >(
                "EchoMetadata", &EchoService::AsyncService::RequestEchoMetadata,
                std::bind(&EchoServiceImpl::echo_request_metadata, this, std::placeholders::_1))) {
            LOGERROR("register rpc failed");
            return false;
        }
        return true;
    }
};

class AuthBaseTest : public ::testing::Test {
public:
    void SetUp() override {}

    void TearDown() override {
        if (m_grpc_server) {
            m_grpc_server->shutdown();
            delete m_grpc_server;
            delete m_echo_impl;
        }
    }

    void grpc_server_start(const std::string& server_address, std::shared_ptr< AuthManager > auth_mgr) {
        LOGINFO("Start echo and ping server on {}...", server_address);
        m_grpc_server = GrpcServer::make(server_address, auth_mgr, 4, "", "");
        m_echo_impl = new EchoServiceImpl();
        m_echo_impl->register_service(m_grpc_server);
        m_grpc_server->register_async_generic_service();
        m_grpc_server->run();
        LOGINFO("Server listening on {}", server_address);
        m_echo_impl->register_rpcs(m_grpc_server);
        m_grpc_server->register_generic_rpc(GENERIC_METHOD,
                                            [](boost::intrusive_ptr< GenericRpcData >&) { return true; });
    }

    void process_echo_reply() {
        m_echo_received.store(true);
        m_cv.notify_all();
    }

    void call_async_echo(EchoRequest& req, EchoReply& reply, ::grpc::Status& status) {
        m_echo_stub->call_unary< EchoRequest, EchoReply >(
            req, &EchoService::StubInterface::AsyncEcho,
            [&reply, &status, this](EchoReply& reply_, ::grpc::Status& status_) {
                reply = reply_;
                status = status_;
                process_echo_reply();
            },
            1);
        {
            std::unique_lock lk(m_wait_mtx);
            m_cv.wait(lk, [this]() { return m_echo_received.load(); });
        }
    }

    void call_async_generic_rpc(::grpc::Status& status) {
        ::grpc::ByteBuffer req;
        m_generic_stub->call_unary(
            req, GENERIC_METHOD,
            [&status, this](::grpc::ByteBuffer&, ::grpc::Status& status_) {
                status = status_;
                m_generic_received.store(true);
                m_cv.notify_all();
            },
            1);
        {
            std::unique_lock lk(m_wait_mtx);
            m_cv.wait(lk, [this]() { return m_generic_received.load(); });
        }
    }

    void call_async_echo_metadata(EchoRequest& req, EchoReply& reply, ::grpc::Status& status) {
        m_echo_stub->call_unary< EchoRequest, EchoReply >(
            req, &EchoService::StubInterface::AsyncEchoMetadata,
            [&reply, &status, this](EchoReply& reply_, ::grpc::Status& status_) {
                reply = reply_;
                status = status_;
                process_echo_reply();
            },
            1, grpc_metadata);
        {
            std::unique_lock lk(m_wait_mtx);
            m_cv.wait(lk, [this]() { return m_echo_received.load(); });
        }
    }

protected:
    std::shared_ptr< AuthManager > m_auth_mgr;
    EchoServiceImpl* m_echo_impl = nullptr;
    GrpcServer* m_grpc_server = nullptr;
    std::unique_ptr< GrpcAsyncClient > m_async_grpc_client;
    std::unique_ptr< GrpcAsyncClient::AsyncStub< EchoService > > m_echo_stub;
    std::unique_ptr< GrpcAsyncClient::GenericAsyncStub > m_generic_stub;
    std::atomic_bool m_echo_received{false};
    std::atomic_bool m_generic_received{false};
    std::mutex m_wait_mtx;
    std::condition_variable m_cv;
};

class AuthDisableTest : public AuthBaseTest {
public:
    void SetUp() override {
        // start grpc server without auth
        grpc_server_start(grpc_server_addr, nullptr);

        // Client without auth
        m_async_grpc_client = std::make_unique< GrpcAsyncClient >(grpc_server_addr, "", "");
        m_async_grpc_client->init();
        GrpcAsyncClientWorker::create_worker("worker-1", 4);
        m_echo_stub = m_async_grpc_client->make_stub< EchoService >("worker-1");
        m_generic_stub = m_async_grpc_client->make_generic_stub("worker-1");
    }

    void TearDown() override { AuthBaseTest::TearDown(); }
};

TEST_F(AuthDisableTest, allow_on_disabled_mode) {
    EchoRequest req;
    // server sets the same message as response
    req.set_message("dummy_msg");
    EchoReply reply;
    ::grpc::Status status;
    call_async_echo(req, reply, status);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(req.message(), reply.message());

    ::grpc::Status generic_status;
    call_async_generic_rpc(status);
    EXPECT_TRUE(generic_status.ok());
}

TEST_F(AuthDisableTest, metadata) {
    EchoRequest req;
    EchoReply reply;
    ::grpc::Status status;
    call_async_echo_metadata(req, reply, status);
    EXPECT_TRUE(status.ok());
}

static auto const grant_path = std::string{"dummy_grant.cg"};

static void load_auth_settings() {
    std::ofstream outfile{grant_path};
    outfile << "dummy cg contents\n";
    outfile.close();
    SECURITY_SETTINGS_FACTORY().modifiable_settings([](auto& s) {
        s.auth_manager->auth_allowed_apps = "app1, testapp, app2";
        s.auth_manager->tf_token_url = "http://127.0.0.1";
        s.auth_manager->expiry_leeway_secs = 0;
        s.auth_manager->issuer = "trustfabric";
        s.trf_client->grant_path = grant_path;
        s.trf_client->server = fmt::format("{}:{}/token", trf_token_server_ip, trf_token_server_port);
    });
    SECURITY_SETTINGS_FACTORY().save();
}

static void remove_auth_settings() {
    auto const grant_fs_path = std::filesystem::path{grant_path};
    EXPECT_TRUE(std::filesystem::remove(grant_fs_path));
}

class AuthServerOnlyTest : public AuthBaseTest {
public:
    void SetUp() override {
        // start grpc server with auth
        load_auth_settings();
        m_auth_mgr = std::shared_ptr< AuthManager >(new AuthManager());
        grpc_server_start(grpc_server_addr, m_auth_mgr);

        // Client without auth
        m_async_grpc_client = std::make_unique< GrpcAsyncClient >(grpc_server_addr, "", "");
        m_async_grpc_client->init();
        GrpcAsyncClientWorker::create_worker("worker-2", 4);
        m_echo_stub = m_async_grpc_client->make_stub< EchoService >("worker-2");
        m_generic_stub = m_async_grpc_client->make_generic_stub("worker-2");
    }

    void TearDown() override {
        AuthBaseTest::TearDown();
        remove_auth_settings();
    }
};

TEST_F(AuthServerOnlyTest, fail_on_no_client_auth) {
    EchoRequest req;
    // server sets the same message as response
    req.set_message("dummy_msg");
    EchoReply reply;
    ::grpc::Status status;
    call_async_echo(req, reply, status);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), ::grpc::UNAUTHENTICATED);
    EXPECT_EQ(status.error_message(), "missing header authorization");

    ::grpc::Status generic_status;
    call_async_generic_rpc(generic_status);
    EXPECT_EQ(generic_status.error_code(), ::grpc::UNAUTHENTICATED);
}

class TokenApiImpl : public TokenApi {
public:
    void get_token_impl(Pistache::Http::ResponseWriter& response) {
        LOGINFO("Sending token to client");
        response.send(Pistache::Http::Code::Ok, token_response);
    }

    void get_key_impl(Pistache::Http::ResponseWriter& response) {
        LOGINFO("Download rsa key");
        response.send(Pistache::Http::Code::Ok, rsa_pub_key);
    }
};

class AuthEnableTest : public AuthBaseTest {
public:
    void SetUp() override {
        // start grpc server with auth
        load_auth_settings();
        m_auth_mgr = std::shared_ptr< AuthManager >(new AuthManager());
        grpc_server_start(grpc_server_addr, m_auth_mgr);

        // start token server
        APIBase::init(Pistache::Address(fmt::format("{}:{}", trf_token_server_ip, trf_token_server_port)), 1);
        m_token_server = std::unique_ptr< TokenApiImpl >(new TokenApiImpl());
        m_token_server->setupRoutes();
        APIBase::start();

        // Client with auth
        m_trf_client = std::make_shared< TrfClient >();
        m_async_grpc_client = std::make_unique< GrpcAsyncClient >(grpc_server_addr, m_trf_client, "", "");
        m_async_grpc_client->init();
        GrpcAsyncClientWorker::create_worker("worker-3", 4);
        m_echo_stub = m_async_grpc_client->make_stub< EchoService >("worker-3");
        m_generic_stub = m_async_grpc_client->make_generic_stub("worker-3");
    }

    void TearDown() override {
        AuthBaseTest::TearDown();
        APIBase::stop();
        remove_auth_settings();
    }

protected:
    std::unique_ptr< TokenApiImpl > m_token_server;
    std::shared_ptr< TrfClient > m_trf_client;
};

TEST_F(AuthEnableTest, allow_with_auth) {
    auto raw_token = TestToken().sign_rs256();
    set_token_response(raw_token);
    EchoRequest req;
    req.set_message("dummy_msg");
    EchoReply reply;
    ::grpc::Status status;
    call_async_echo(req, reply, status);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(req.message(), reply.message());

    ::grpc::Status generic_status;
    call_async_generic_rpc(status);
    EXPECT_TRUE(generic_status.ok());
}

// sync client
class EchoAndPingClient : public GrpcSyncClient {

public:
    using GrpcSyncClient::GrpcSyncClient;
    void init() override {
        GrpcSyncClient::init();
        echo_stub_ = MakeStub< EchoService >();
    }

    const std::unique_ptr< EchoService::StubInterface >& echo_stub() { return echo_stub_; }

private:
    std::unique_ptr< EchoService::StubInterface > echo_stub_;
};

TEST_F(AuthEnableTest, allow_sync_client_with_auth) {
    auto sync_client = std::make_unique< EchoAndPingClient >(grpc_server_addr, "", "");
    sync_client->init();
    EchoRequest req;
    EchoReply reply;
    req.set_message("dummy_sync_msg");
    ::grpc::ClientContext context;
    context.AddMetadata("authorization", m_trf_client->get_typed_token());
    auto status = sync_client->echo_stub()->Echo(&context, req, &reply);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(req.message(), reply.message());
}

void validate_generic_reply(const std::string& method, ::grpc::Status& status) {
    if (method == "method1" || method == "method2") {
        EXPECT_TRUE(status.ok());
    } else {
        EXPECT_EQ(status.error_code(), ::grpc::UNIMPLEMENTED);
    }
}

TEST(GenericServiceDeathTest, basic_test) {
    testing::GTEST_FLAG(death_test_style) = "threadsafe";
    auto g_grpc_server = GrpcServer::make("0.0.0.0:56789", nullptr, 1, "", "");
    // register rpc before generic service is registered
#ifndef NDEBUG
    ASSERT_DEATH(
        g_grpc_server->register_generic_rpc("method1", [](boost::intrusive_ptr< GenericRpcData >&) { return true; }),
        "Assertion .* failed");
#else
    EXPECT_FALSE(
        g_grpc_server->register_generic_rpc("method1", [](boost::intrusive_ptr< GenericRpcData >&) { return true; }));
#endif

    ASSERT_TRUE(g_grpc_server->register_async_generic_service());
    // duplicate register
    EXPECT_FALSE(g_grpc_server->register_async_generic_service());
    // register rpc before server is run
#ifndef NDEBUG
    ASSERT_DEATH(
        g_grpc_server->register_generic_rpc("method1", [](boost::intrusive_ptr< GenericRpcData >&) { return true; }),
        "Assertion .* failed");
#else
    EXPECT_FALSE(
        g_grpc_server->register_generic_rpc("method1", [](boost::intrusive_ptr< GenericRpcData >&) { return true; }));
#endif
    g_grpc_server->run();
    EXPECT_TRUE(
        g_grpc_server->register_generic_rpc("method1", [](boost::intrusive_ptr< GenericRpcData >&) { return true; }));
    EXPECT_TRUE(
        g_grpc_server->register_generic_rpc("method2", [](boost::intrusive_ptr< GenericRpcData >&) { return true; }));
    // re-register method 1
    EXPECT_FALSE(
        g_grpc_server->register_generic_rpc("method1", [](boost::intrusive_ptr< GenericRpcData >&) { return true; }));

    auto client = std::make_unique< GrpcAsyncClient >("0.0.0.0:56789", "", "");
    client->init();
    GrpcAsyncClientWorker::create_worker("generic_worker", 1);
    auto generic_stub = client->make_generic_stub("generic_worker");
    ::grpc::ByteBuffer cli_buf;
    generic_stub->call_unary(
        cli_buf, "method1",
        [method = "method1"](::grpc::ByteBuffer&, ::grpc::Status& status) { validate_generic_reply(method, status); },
        1);
    generic_stub->call_unary(
        cli_buf, "method2",
        [method = "method2"](::grpc::ByteBuffer&, ::grpc::Status& status) { validate_generic_reply(method, status); },
        1);
    generic_stub->call_unary(
        cli_buf, "method_unknown",
        [method = "method_unknown"](::grpc::ByteBuffer&, ::grpc::Status& status) {
            validate_generic_reply(method, status);
        },
        1);
}

} // namespace sisl::grpc::testing

int main(int argc, char* argv[]) {
    ::testing::InitGoogleMock(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger("auth_test");
    int ret{RUN_ALL_TESTS()};
    sisl::GrpcAsyncClientWorker::shutdown_all();
    return ret;
}
