/**
 * The following test cases are taken from OM.
 * https://github.corp.ebay.com/SDS/om_cpp/blob/master/src/tests/unit/Middleware/AuthTest.cpp
 **/

#include "http_server.hpp"
#include <memory>
#include <thread>
#include <condition_variable>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

SDS_LOGGING_INIT(httpserver_lmod)
SDS_OPTIONS_ENABLE(logging)

namespace sisl::testing {
using namespace ::testing;

/**
 * Load public and private keys.
 * Assume the keys(id_rsa.pub and id_rsa) are in the same directory as this file
 */
static const std::string cur_file_dir{__FILE__};
static const std::string load_test_data(const std::string& file_name) {
    auto last_slash_pos = cur_file_dir.rfind('/');
    if (last_slash_pos == std::string::npos) { return ""; }
    const std::string key_base{cur_file_dir.substr(0, last_slash_pos + 1)};
    std::ifstream f(fmt::format("{}/{}", key_base, file_name));
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
    MOCK_METHOD(std::string, download_key, (const std::string&));
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
        mock_auth_mgr = std::unique_ptr< MockAuthManager >(new MockAuthManager());
        mock_auth_mgr->set_config(auth_cfg);
        mock_server = std::unique_ptr< HttpServer >(new HttpServer(
            cfg, mock_auth_mgr, {handler_info("/api/v1/sayHello", AuthBaseTest::say_hello, (void*)this)}));
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

} // namespace sisl::testing

using namespace sisl;
using namespace sisl::testing;

int main(int argc, char* argv[]) {
    ::testing::InitGoogleMock(&argc, argv);
    SDS_OPTIONS_LOAD(argc, argv, logging)
    return RUN_ALL_TESTS();
}
