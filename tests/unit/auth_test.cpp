#include <memory>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <mutex>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/auth_manager/trf_client.hpp>

#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
#include <sisl/async_http/http_server.hpp>
#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic pop
#endif

#include "grpc_helper/rpc_client.hpp"
#include "grpc_helper/rpc_server.hpp"
#include "grpc_helper_test.grpc.pb.h"
#include "test_token.hpp"

SISL_LOGGING_INIT(logging, grpc_server, httpserver_lmod)
SISL_OPTIONS_ENABLE(logging)

namespace grpc_helper::testing {
using namespace sisl;
using namespace ::grpc_helper_test;
using namespace ::testing;

static const std::string grpc_server_addr{"0.0.0.0:12345"};
static const std::string trf_token_server_ip{"127.0.0.1"};
static const uint32_t trf_token_server_port{12346};

class EchoServiceImpl {
public:
    virtual ~EchoServiceImpl() = default;

    virtual bool echo_request(const AsyncRpcDataPtr< EchoService, EchoRequest, EchoReply >& rpc_data) {
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
    virtual void SetUp() {}

    virtual void TearDown() {}

    void grpc_server_start(const std::string& server_address, std::shared_ptr< AuthManager > auth_mgr) {
        LOGINFO("Start echo and ping server on {}...", server_address);
        m_grpc_server = GrpcServer::make(server_address, 4, "", "", auth_mgr);
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
    virtual void SetUp() {
        // start grpc server without auth
        grpc_server_start(grpc_server_addr, nullptr);

        // Client without auth
        m_async_grpc_client = std::make_unique< GrpcAsyncClient >(grpc_server_addr, "", "");
        m_async_grpc_client->init();
        GrpcAsyncClientWorker::create_worker("worker-1", 4);
        m_echo_stub = m_async_grpc_client->make_stub< EchoService >("worker-1");
    }

    virtual void TearDown() { m_grpc_server->shutdown(); }
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

class AuthServerOnlyTest : public AuthBaseTest {
public:
    virtual void SetUp() {
        // start grpc server with auth
        AuthMgrConfig auth_cfg;
        auth_cfg.tf_token_url = "http://127.0.0.1";
        auth_cfg.auth_allowed_apps = "app1, testapp, app2";
        auth_cfg.auth_exp_leeway = 0;
        m_auth_mgr = std::shared_ptr< AuthManager >(new AuthManager());
        m_auth_mgr->set_config(auth_cfg);
        grpc_server_start(grpc_server_addr, m_auth_mgr);

        // Client without auth
        m_async_grpc_client = std::make_unique< GrpcAsyncClient >(grpc_server_addr, "", "");
        m_async_grpc_client->init();
        GrpcAsyncClientWorker::create_worker("worker-1", 4);
        m_echo_stub = m_async_grpc_client->make_stub< EchoService >("worker-1");
    }

    virtual void TearDown() { m_grpc_server->shutdown(); }
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

static std::string get_cur_file_dir() {
    const std::string cur_file_path{__FILE__};
    auto last_slash_pos = cur_file_path.rfind('/');
    if (last_slash_pos == std::string::npos) { return ""; }
    return std::string{cur_file_path.substr(0, last_slash_pos + 1)};
}

static const std::string cur_file_dir{get_cur_file_dir()};

class AuthEnableTest : public AuthBaseTest {
public:
    virtual void SetUp() {
        // start grpc server with auth
        AuthMgrConfig auth_cfg;
        auth_cfg.tf_token_url = "http://127.0.0.1";
        auth_cfg.auth_allowed_apps = "app1, testapp, app2";
        auth_cfg.auth_exp_leeway = 0;
        auth_cfg.issuer = "trustfabric";
        auth_cfg.verify = false;
        m_auth_mgr = std::shared_ptr< AuthManager >(new AuthManager());
        m_auth_mgr->set_config(auth_cfg);
        grpc_server_start(grpc_server_addr, m_auth_mgr);

        // start token server
        HttpServerConfig http_cfg;
        http_cfg.is_tls_enabled = false;
        http_cfg.bind_address = trf_token_server_ip;
        http_cfg.server_port = trf_token_server_port;
        http_cfg.read_write_timeout_secs = 10;
        http_cfg.is_auth_enabled = false;
        m_token_server = std::unique_ptr< HttpServer >(
            new HttpServer(http_cfg,
                           {handler_info("/token", AuthEnableTest::get_token, (void*)this),
                            handler_info("/download_key", AuthEnableTest::download_key, (void*)this)}));
        m_token_server->start();

        // Client with auth
        TrfClientConfig trf_cfg;
        trf_cfg.leeway = 0;
        trf_cfg.server = fmt::format("{}:{}/token", trf_token_server_ip, trf_token_server_port);
        trf_cfg.verify = false;
        trf_cfg.grant_path = fmt::format("{}/dummy_grant.cg", cur_file_dir);
        std::ofstream outfile(trf_cfg.grant_path);
        outfile << "dummy cg contents\n";
        outfile.close();
        m_trf_client = std::make_shared< TrfClient >(trf_cfg);
        m_async_grpc_client = std::make_unique< GrpcAsyncClient >(grpc_server_addr, "", "", m_trf_client);
        m_async_grpc_client->init();
        GrpcAsyncClientWorker::create_worker("worker-1", 4);
        m_echo_stub = m_async_grpc_client->make_stub< EchoService >("worker-1");
    }

    virtual void TearDown() {
        m_token_server->stop();
        m_grpc_server->shutdown();
    }

    static void get_token(HttpCallData cd) {
        std::string msg;
        std::cout << "sending token to client" << std::endl;
        pThis(cd)->m_token_server->respond_OK(cd, EVHTP_RES_OK, m_token_response);
    }

    static void download_key(HttpCallData cd) {
        std::string msg;
        pThis(cd)->m_token_server->respond_OK(cd, EVHTP_RES_OK, rsa_pub_key);
    }

    static void set_token_response(const std::string& raw_token) {
        m_token_response = "{\n"
                           "  \"access_token\": \"" +
            raw_token +
            "\",\n"
            "  \"token_type\": \"Bearer\",\n"
            "  \"expires_in\": \"2000\",\n"
            "  \"refresh_token\": \"dummy_refresh_token\"\n"
            "}";
    }

protected:
    std::unique_ptr< HttpServer > m_token_server;
    std::shared_ptr< TrfClient > m_trf_client;
    static AuthEnableTest* pThis(HttpCallData cd) { return (AuthEnableTest*)cd->cookie(); }
    static std::string m_token_response;
};
std::string AuthEnableTest::m_token_response;

TEST_F(AuthEnableTest, allow_with_auth) {
    auto raw_token = TestToken().sign_rs256();
    AuthEnableTest::set_token_response(raw_token);
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
    virtual void init() {
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
    return RUN_ALL_TESTS();
}
