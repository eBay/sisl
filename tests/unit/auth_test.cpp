#include <memory>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <mutex>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/auth_manager/trf_client.hpp>

#include "basic_http_server.hpp"
#include "grpc_helper/rpc_client.hpp"
#include "grpc_helper/rpc_server.hpp"
#include "grpc_helper_test.grpc.pb.h"
#include "test_token.hpp"

SISL_LOGGING_INIT(logging, grpc_server)
SISL_OPTIONS_ENABLE(logging)

namespace grpc_helper::testing {
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

class EchoServiceImpl final {
public:
    ~EchoServiceImpl() = default;

    bool echo_request(const AsyncRpcDataPtr< EchoService, EchoRequest, EchoReply >& rpc_data) {
        LOGDEBUG("receive echo request {}", rpc_data->request().message());
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
                std::bind(&EchoServiceImpl::echo_request, this, std::placeholders::_1))) {
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
        m_grpc_server->run();
        LOGINFO("Server listening on {}", server_address);
        m_echo_impl->register_rpcs(m_grpc_server);
    }

    void process_echo_reply() {
        m_echo_received.store(true);
        m_cv.notify_all();
    }

    void call_async_echo(EchoRequest& req, EchoReply& reply, grpc::Status& status) {
        m_echo_stub->call_unary< EchoRequest, EchoReply >(
            req, &EchoService::StubInterface::AsyncEcho,
            [&reply, &status, this](EchoReply& reply_, grpc::Status& status_) {
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

protected:
    std::shared_ptr< AuthManager > m_auth_mgr;
    EchoServiceImpl* m_echo_impl = nullptr;
    GrpcServer* m_grpc_server = nullptr;
    std::unique_ptr< GrpcAsyncClient > m_async_grpc_client;
    std::unique_ptr< GrpcAsyncClient::AsyncStub< EchoService > > m_echo_stub;
    std::atomic_bool m_echo_received{false};
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
    }

    void TearDown() override { AuthBaseTest::TearDown(); }
};

TEST_F(AuthDisableTest, allow_on_disabled_mode) {
    EchoRequest req;
    // server sets the same message as response
    req.set_message("dummy_msg");
    EchoReply reply;
    grpc::Status status;
    call_async_echo(req, reply, status);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(req.message(), reply.message());
}

static auto const grant_path = std::string{"dummy_grant.cg"};

static void load_auth_settings() {
    std::ofstream outfile{grant_path};
    outfile << "dummy cg contents\n";
    outfile.close();
    SECURITY_SETTINGS_FACTORY().modifiable_settings([](auto& s) {
        s.auth_manager->auth_allowed_apps = "app1, testapp, app2";
        s.auth_manager->tf_token_url = "http://127.0.0.1";
        s.auth_manager->leeway = 0;
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
    grpc::Status status;
    call_async_echo(req, reply, status);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::UNAUTHENTICATED);
    EXPECT_EQ(status.error_message(), "missing header authorization");
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
    grpc::Status status;
    call_async_echo(req, reply, status);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(req.message(), reply.message());
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

} // namespace grpc_helper::testing

int main(int argc, char* argv[]) {
    ::testing::InitGoogleMock(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger("auth_test");
    int ret{RUN_ALL_TESTS()};
    grpc_helper::GrpcAsyncClientWorker::shutdown_all();
    return ret;
}
