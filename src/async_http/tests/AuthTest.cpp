/**
 * The following test cases are taken from OM.
 * https://github.corp.ebay.com/SDS/om_cpp/blob/master/src/tests/unit/Middleware/AuthTest.cpp
 **/

#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "auth_manager/trf_client.hpp"
#include "http_server.hpp"

SISL_LOGGING_INIT(httpserver_lmod)
SISL_OPTIONS_ENABLE(logging)

namespace sisl::testing {
using namespace ::testing;

/**
 * Load public and private keys.
 * Assume the keys(id_rsa.pub and id_rsa) are in the same directory as this file
 */

static std::string get_cur_file_dir() {
    const std::string cur_file_path{__FILE__};
    auto last_slash_pos = cur_file_path.rfind('/');
    if (last_slash_pos == std::string::npos) { return ""; }
    return std::string{cur_file_path.substr(0, last_slash_pos + 1)};
}

static const std::string cur_file_dir{get_cur_file_dir()};

static const std::string load_test_data(const std::string& file_name) {
    std::ifstream f(fmt::format("{}/{}", cur_file_dir, file_name));
    std::string buffer(std::istreambuf_iterator< char >{f}, std::istreambuf_iterator< char >{});
    if (!buffer.empty() && std::isspace(buffer.back())) buffer.pop_back();
    return buffer;
}

static const std::string rsa_pub_key{load_test_data("id_rsa.pub")};
static const std::string rsa_priv_key{load_test_data("id_rsa")};
static const std::string rsa_pub1_key{load_test_data("id_rsa1.pub")};

/**
 * This will by default construct a valid jwt token, which contains exactly the
 * same attributes in heeader and payload claims. In some test cases if we want
 * to build a token with some invalid attributes, we must explicitly set those
 * attributes.
 *
 * A trustfabric token:
 * Header claims <key-value pairs>
 *   alg: RS256
 *   kid: 779112af
 *   typ: JWT
 *   x5u: https://trustfabric.vip.ebay.com/v2/k/779112af
 *
 * Payload claims <key-value pairs>
 *   iss: trustfabric
 *   aud: [usersessionauthsvc, protegoreg, fountauth, monstor, ...]
 *   cluster: 92
 *   ns: sds-tess92-19
 *   iat: 1610081499
 *   exp: 1610083393
 *   nbf: 1610081499
 *   instances: 10.175.165.15
 *   sub:
 * uid=sdsapp,networkaddress=10.175.165.15,ou=orchmanager+l=production,o=sdstess9219,dc=tess,dc=ebay,dc=com
 *   ver: 2
 *   vpc: production
 */
struct TestToken {
    using token_t = jwt::builder;

    TestToken() :
            token{jwt::create()
                      .set_type("JWT")
                      .set_algorithm("RS256")
                      .set_key_id("abc123")
                      .set_issuer("trustfabric")
                      .set_header_claim("x5u", jwt::claim(std::string{"http://127.0.0.1:12347/dummy_tf_token"}))
                      .set_audience(std::set< std::string >{"test-sisl", "protegoreg"})
                      .set_issued_at(std::chrono::system_clock::now() - std::chrono::seconds(180))
                      .set_not_before(std::chrono::system_clock::now() - std::chrono::seconds(180))
                      .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds(180))
                      .set_subject("uid=sdsapp,networkaddress=10.175.165.15,ou=orchmanager+l="
                                   "production,o=testapp,dc=tess,dc=ebay,dc=com")
                      .set_payload_claim("ver", jwt::claim(std::string{"2"}))
                      .set_payload_claim("vpc", jwt::claim(std::string{"production"}))
                      .set_payload_claim("instances", jwt::claim(std::string{"10.175.65.15"}))} {}

    std::string sign_rs256() { return token.sign(jwt::algorithm::rs256(rsa_pub_key, rsa_priv_key, "", "")); }
    std::string sign_rs512() { return token.sign(jwt::algorithm::rs512(rsa_pub_key, rsa_priv_key, "", "")); }
    token_t& get_token() { return token; }

private:
    token_t token;
};

class MockAuthManager : public AuthManager {
public:
    using AuthManager::AuthManager;
    MOCK_METHOD(std::string, download_key, (const std::string&), (const));
};

class AuthBaseTest : public ::testing::Test {
public:
    virtual void SetUp() {
        cfg.is_tls_enabled = false;
        cfg.bind_address = "127.0.0.1";
        cfg.server_port = 12345;
        cfg.read_write_timeout_secs = 10;
    }

    virtual void TearDown() { mock_server->stop(); }

    static void say_hello(HttpCallData cd) {
        std::string msg;
        if (auto r = pThis(cd)->mock_server->http_auth_verify(cd->request(), msg); r != EVHTP_RES_OK) {
            pThis(cd)->mock_server->respond_NOTOK(cd, r, msg);
            return;
        }
        std::cout << "Client is saying hello\n";
        pThis(cd)->mock_server->respond_OK(cd, EVHTP_RES_OK, "Hello client from async_http server\n");
    }

protected:
    HttpServerConfig cfg;
    std::unique_ptr< HttpServer > mock_server;
    static AuthBaseTest* pThis(HttpCallData cd) { return (AuthBaseTest*)cd->cookie(); }
};

class AuthEnableTest : public AuthBaseTest {
public:
    virtual void SetUp() {
        AuthBaseTest::SetUp();
        cfg.is_auth_enabled = true;
        AuthMgrConfig auth_cfg;
        auth_cfg.tf_token_url = "http://127.0.0.1";
        auth_cfg.auth_allowed_apps = "app1, testapp, app2";
        auth_cfg.auth_exp_leeway = 0;
        auth_cfg.issuer = "trustfabric";
        mock_auth_mgr = std::shared_ptr< MockAuthManager >(new MockAuthManager());
        mock_auth_mgr->set_config(auth_cfg);
        mock_server = std::unique_ptr< HttpServer >(new HttpServer(
            cfg, {handler_info("/api/v1/sayHello", AuthBaseTest::say_hello, (void*)this)}, mock_auth_mgr));
        mock_server->start();
    }

    virtual void TearDown() { AuthBaseTest::TearDown(); }

    void set_allowed_to_all() { mock_server->set_allowed_to_all(); }

protected:
    std::shared_ptr< MockAuthManager > mock_auth_mgr;
};

class AuthDisableTest : public AuthBaseTest {
public:
    virtual void SetUp() {
        AuthBaseTest::SetUp();
        cfg.is_auth_enabled = false;
        mock_server = std::unique_ptr< HttpServer >(
            new HttpServer(cfg, {handler_info("/api/v1/sayHello", AuthBaseTest::say_hello, (void*)this)}));
        mock_server->start();
    }

    virtual void TearDown() { AuthBaseTest::TearDown(); }
};

// test the TestToken utility, should not raise
TEST(TokenGenerte, sign_and_decode) {
    auto token = TestToken().sign_rs256();
    auto verify = jwt::verify().allow_algorithm(jwt::algorithm::rs256(rsa_pub_key)).with_issuer("trustfabric");
    auto decoded = jwt::decode(token);
    verify.verify(decoded);
}

TEST_F(AuthDisableTest, allow_all_on_disabled_mode) {
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    auto resp = cpr::Post(url);
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(200, resp.status_code);
}

TEST_F(AuthEnableTest, reject_all_on_enabled_mode) {
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    auto resp = cpr::Post(url);
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(401, resp.status_code);
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(0);
}

TEST_F(AuthEnableTest, allow_vaid_token) {
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    auto resp = cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", TestToken().sign_rs256())}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(200, resp.status_code);
}

TEST_F(AuthEnableTest, reject_basic_auth) {
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(0);
    // has basic auth in requester header, we require bearer token
    auto resp = cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Basic {}", TestToken().sign_rs256())}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(401, resp.status_code);
}

TEST_F(AuthEnableTest, reject_garbage_auth) {
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(0);
    auto resp = cpr::Post(url, cpr::Header{{"Authorization", "Bearer abcdefgh"}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(401, resp.status_code);
}

TEST_F(AuthEnableTest, reject_wrong_algorithm) {
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    // we currently only support rs256
    auto resp = cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", TestToken().sign_rs512())}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(401, resp.status_code);
}

TEST_F(AuthEnableTest, reject_untrusted_issuer) {
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    // token is issued by an untrusted issuer, we only trust "trustfabric"
    auto token = TestToken();
    token.get_token().set_issuer("do_not_trust_me");
    auto resp = cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", token.sign_rs256())}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(401, resp.status_code);
}

TEST_F(AuthEnableTest, reject_untrusted_keyurl) {
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(0);
    // the key url is an untrusted address, we only trust "http://127.0.0.1"
    auto token = TestToken();
    token.get_token().set_header_claim("x5u", jwt::claim(std::string{"http://untrusted.addr/keys/abc123"}));
    auto resp = cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", token.sign_rs256())}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(401, resp.status_code);
}

TEST_F(AuthEnableTest, reject_expired_token) {
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    // token expired 1 second ago
    auto token = TestToken();
    token.get_token().set_expires_at(std::chrono::system_clock::now() - std::chrono::seconds(1));
    auto resp = cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", token.sign_rs256())}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(401, resp.status_code);
}

TEST_F(AuthEnableTest, reject_download_key_fail) {
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Throw(std::runtime_error("download key failed")));
    auto resp = cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", TestToken().sign_rs256())}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(401, resp.status_code);
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(0);
}

TEST_F(AuthEnableTest, reject_wrong_key) {
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub1_key));
    auto resp = cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", TestToken().sign_rs256())}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(401, resp.status_code);
}

TEST_F(AuthEnableTest, allow_all_apps) {
    set_allowed_to_all();
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    auto token = TestToken();
    token.get_token().set_subject("any-prefix,o=dummy_app,dc=tess,dc=ebay,dc=com");
    auto resp = cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", token.sign_rs256())}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(200, resp.status_code);
}

TEST_F(AuthEnableTest, reject_unauthorized_app) {
    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    // the client application is "myapp", which is not in the allowed list
    auto token = TestToken();
    token.get_token().set_subject("any-prefix,o=myapp,dc=tess,dc=ebay,dc=com");
    auto resp = cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", token.sign_rs256())}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(403, resp.status_code);
}

// Testing trf client
class MockTrfClient : public TrfClient {
public:
    using TrfClient::TrfClient;
    MOCK_METHOD(void, request_with_grant_token, ());
    void set_token(const std::string& raw_token, const std::string token_type) {
        m_access_token = raw_token;
        m_token_type = token_type;
        m_expiry = std::chrono::system_clock::now() + std::chrono::seconds(2000);
    }
    // deligate to parent class (run the real method)

    void __request_with_grant_token() { TrfClient::request_with_grant_token(); }

    void set_expiry(std::chrono::system_clock::time_point tp) { m_expiry = tp; }
    std::string get_access_token() { return m_access_token; }
    std::string get_token_type() { return m_token_type; }
};

// this test will take 10 seconds to run
TEST_F(AuthEnableTest, trf_grant_path_failure) {
    EXPECT_THROW(
        {
            try {
                TrfClientConfig cfg;
                cfg.grant_path = "dummy_path";
                TrfClient trf_client(cfg);
            } catch (const std::runtime_error& e) {
                EXPECT_STREQ(e.what(), "trustfabric client grant path dummy_path does not exist");
                throw e;
            }
        },
        std::runtime_error);
}

TEST_F(AuthEnableTest, trf_allow_valid_token) {
    TrfClientConfig cfg;
    cfg.grant_path = fmt::format("{}/dummy_grant.cg", cur_file_dir);
    cfg.leeway = 30;
    std::ofstream outfile(cfg.grant_path);
    outfile.close();
    MockTrfClient mock_trf_client(cfg);
    auto raw_token = TestToken().sign_rs256();
    // mock_trf_client is expected to be called twice
    // 1. First time when access_token is empty
    // 2. When token is set to be expired
    EXPECT_CALL(mock_trf_client, request_with_grant_token()).Times(2);
    ON_CALL(mock_trf_client, request_with_grant_token())
        .WillByDefault(
            testing::Invoke([&mock_trf_client, &raw_token]() { mock_trf_client.set_token(raw_token, "Bearer"); }));

    cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    auto resp = cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", mock_trf_client.get_token())}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(200, resp.status_code);

    // use the acces_token saved from the previous call
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    resp = cpr::Post(url, cpr::Header{{"Authorization", mock_trf_client.get_typed_token()}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(200, resp.status_code);

    // set token to be expired invoking request_with_grant_token
    mock_trf_client.set_expiry(std::chrono::system_clock::now() - std::chrono::seconds(100));
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    resp = cpr::Post(url, cpr::Header{{"Authorization", mock_trf_client.get_typed_token()}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(200, resp.status_code);
}

// Test request_with_grant_token. Setup http server with path /token to return token json
class TrfClientTest : public ::testing::Test {
public:
    virtual void SetUp() {
        cfg.is_tls_enabled = false;
        cfg.bind_address = "127.0.0.1";
        cfg.server_port = 12345;
        cfg.read_write_timeout_secs = 10;
        cfg.is_auth_enabled = false;
        mock_server = std::unique_ptr< HttpServer >(
            new HttpServer(cfg, {handler_info("/token", TrfClientTest::get_token, (void*)this)}));
        mock_server->start();
    }

    virtual void TearDown() { mock_server->stop(); }

    static void get_token(HttpCallData cd) {
        std::string msg;
        if (auto r = pThis(cd)->mock_server->http_auth_verify(cd->request(), msg); r != EVHTP_RES_OK) {
            pThis(cd)->mock_server->respond_NOTOK(cd, r, msg);
            return;
        }
        std::cout << "sending token to client" << std::endl;
        pThis(cd)->mock_server->respond_OK(cd, EVHTP_RES_OK, m_token_response);
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
    HttpServerConfig cfg;
    std::unique_ptr< HttpServer > mock_server;
    static TrfClientTest* pThis(HttpCallData cd) { return (TrfClientTest*)cd->cookie(); }
    static std::string m_token_response;
};
std::string TrfClientTest::m_token_response;

TEST_F(TrfClientTest, trf_grant_path_load_failure) {
    TrfClientConfig cfg;
    cfg.grant_path = fmt::format("{}/dummy_grant.cg", cur_file_dir);
    std::ofstream outfile(cfg.grant_path);
    outfile.close();
    MockTrfClient mock_trf_client(cfg);
    EXPECT_CALL(mock_trf_client, request_with_grant_token()).Times(1);
    ON_CALL(mock_trf_client, request_with_grant_token()).WillByDefault(testing::Invoke([&mock_trf_client]() {
        mock_trf_client.__request_with_grant_token();
    }));
    EXPECT_THROW(
        {
            try {
                mock_trf_client.get_token();
            } catch (const std::runtime_error& e) {
                EXPECT_EQ(e.what(), fmt::format("could not load grant from path {}", cfg.grant_path));
                throw e;
            }
        },
        std::runtime_error);
}

TEST_F(TrfClientTest, request_with_grant_token) {
    TrfClientConfig cfg;
    cfg.grant_path = fmt::format("{}/dummy_grant.cg", cur_file_dir);
    cfg.verify = false;
    cfg.server = "127.0.0.1:12345/token";
    std::ofstream outfile(cfg.grant_path);
    outfile << "dummy cg contents\n";
    outfile.close();
    MockTrfClient mock_trf_client(cfg);
    auto raw_token = TestToken().sign_rs256();
    TrfClientTest::set_token_response(raw_token);
    EXPECT_CALL(mock_trf_client, request_with_grant_token()).Times(1);
    ON_CALL(mock_trf_client, request_with_grant_token()).WillByDefault(testing::Invoke([&mock_trf_client]() {
        mock_trf_client.__request_with_grant_token();
    }));
    mock_trf_client.get_token();
    EXPECT_EQ(raw_token, mock_trf_client.get_access_token());
    EXPECT_EQ("Bearer", mock_trf_client.get_token_type());
}

} // namespace sisl::testing

using namespace sisl;
using namespace sisl::testing;

int main(int argc, char* argv[]) {
    ::testing::InitGoogleMock(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging)
    return RUN_ALL_TESTS();
}
