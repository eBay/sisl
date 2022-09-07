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

SISL_OPTIONS_ENABLE(logging)

namespace sisl::testing {
using namespace ::testing;

/**
 * Load public and private keys.
 * Assume the keys(id_rsa.pub and id_rsa) are in the same directory as this file
 */

static std::string get_cur_file_dir() {
    const std::string cur_file_path{__FILE__};
    const auto last_slash_pos{cur_file_path.rfind('/')};
    if (last_slash_pos == std::string::npos) { return ""; }
    return std::string{cur_file_path.substr(0, last_slash_pos + 1)};
}

static const std::string cur_file_dir{get_cur_file_dir()};

static const std::string grant_path = fmt::format("{}/dummy_grant.cg", cur_file_dir);

static const std::string load_test_data(const std::string& file_name) {
    std::ifstream f{fmt::format("{}/{}", cur_file_dir, file_name)};
    std::string buffer{std::istreambuf_iterator< char >{f}, std::istreambuf_iterator< char >{}};
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
    AuthVerifyStatus verify(const std::string& token) { return verify(token, ""); }
};

class AuthTest : public ::testing::Test {
public:
    virtual void SetUp() override { load_settings(); }

    virtual void TearDown() override {}

    void set_allowed_to_all() {
        SECURITY_SETTINGS_FACTORY().modifiable_settings([](auto& s) { s.auth_manager->auth_allowed_apps = "all"; });
        SECURITY_SETTINGS_FACTORY().save();
    }

    static void load_settings() {
        SECURITY_SETTINGS_FACTORY().modifiable_settings([](auto& s) {
            s.auth_manager->auth_allowed_apps = "app1, testapp, app2";
            s.auth_manager->tf_token_url = "http://127.0.0.1";
            s.auth_manager->leeway = 0;
            s.auth_manager->issuer = "trustfabric";
        });
        SECURITY_SETTINGS_FACTORY().save();
    }

protected:
    std::shared_ptr< MockAuthManager > mock_auth_mgr;
};

// test the TestToken utility, should not raise
TEST(TokenGenerte, sign_and_decode) {
    const auto token{TestToken().sign_rs256()};
    const auto verify{jwt::verify().allow_algorithm(jwt::algorithm::rs256(rsa_pub_key)).with_issuer("trustfabric")};
    const auto decoded{jwt::decode(token)};
    verify.verify(decoded);
}

TEST_F(AuthTest, allow_vaid_token) {
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    EXPECT_EQ(mock_auth_mgr->verify(TestToken().sign_rs256()), AuthVerifyStatus::OK);
}

TEST_F(AuthTest, reject_garbage_auth) {
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    EXPECT_EQ(mock_auth_mgr->verify("garbage_token"), AuthVerifyStatus::UNAUTH);
}

TEST_F(AuthTest, reject_wrong_algorithm) {
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    EXPECT_EQ(mock_auth_mgr->verify(TestToken().sign_rs512()), AuthVerifyStatus::UNAUTH);
}

TEST_F(AuthTest, reject_untrusted_issuer) {
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    // token is issued by an untrusted issuer, we only trust "trustfabric"
    auto token{TestToken()};
    token.get_token().set_issuer("do_not_trust_me");
    EXPECT_EQ(mock_auth_mgr->verify(TestToken().sign_rs256()), AuthVerifyStatus::UNAUTH);
}

TEST_F(AuthTest, reject_untrusted_keyurl) {
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(0);
    // the key url is an untrusted address, we only trust "http://127.0.0.1"
    auto token{TestToken()};
    token.get_token().set_header_claim("x5u", jwt::claim(std::string{"http://untrusted.addr/keys/abc123"}));
    EXPECT_EQ(mock_auth_mgr->verify(TestToken().sign_rs256()), AuthVerifyStatus::UNAUTH);
}

TEST_F(AuthTest, reject_expired_token) {
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    // token expired 1 second ago
    auto token{TestToken()};
    token.get_token().set_expires_at(std::chrono::system_clock::now() - std::chrono::seconds(1));
    EXPECT_EQ(mock_auth_mgr->verify(TestToken().sign_rs256()), AuthVerifyStatus::UNAUTH);
}
/*

TEST_F(AuthTest, reject_download_key_fail) {
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Throw(std::runtime_error("download key failed")));
    const auto resp{cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", TestToken().sign_rs256())}})};
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(cpr::status::HTTP_UNAUTHORIZED, resp.status_code);
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(0);
}

TEST_F(AuthTest, reject_wrong_key) {
    const cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub1_key));
    const auto resp{cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", TestToken().sign_rs256())}})};
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(cpr::status::HTTP_UNAUTHORIZED, resp.status_code);
}

TEST_F(AuthTest, allow_all_apps) {
    set_allowed_to_all();
    const cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    auto token{TestToken()};
    token.get_token().set_subject("any-prefix,o=dummy_app,dc=tess,dc=ebay,dc=com");
    const auto resp{cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", token.sign_rs256())}})};
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(cpr::status::HTTP_OK, resp.status_code);
}

TEST_F(AuthTest, reject_unauthorized_app) {
    const cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    // the client application is "myapp", which is not in the allowed list
    auto token{TestToken()};
    token.get_token().set_subject("any-prefix,o=myapp,dc=tess,dc=ebay,dc=com");
    const auto resp{cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", token.sign_rs256())}})};
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(cpr::status::HTTP_FORBIDDEN, resp.status_code);

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

static void load_trf_settings() {
    std::ofstream outfile{grant_path};
    outfile << "dummy cg contents\n";
    outfile.close();
    SECURITY_SETTINGS_FACTORY().modifiable_settings([](auto& s) {
        s.trf_client->grant_path = grant_path;
        s.trf_client->server = "127.0.0.1:12345/token";
        s.auth_manager->verify = false;
        s.auth_manager->leeway = 30;
    });
    SECURITY_SETTINGS_FACTORY().save();
}

static void remove_grant_path() { std::remove(grant_path.c_str()); }

// this test will take 10 seconds to run
TEST_F(AuthTest, trf_grant_path_failure) {
    load_trf_settings();
    remove_grant_path();
    EXPECT_THROW(
        {
            try {
                TrfClient trf_client;
            } catch (const std::runtime_error& e) {
                const std::string cmp_string{
                    fmt::format("trustfabric client grant path {} does not exist", grant_path)};
                EXPECT_STREQ(e.what(), cmp_string.c_str());
                throw e;
            }
        },
        std::runtime_error);
}

TEST_F(AuthTest, trf_allow_valid_token) {
    load_trf_settings();
    MockTrfClient mock_trf_client;
    const auto raw_token{TestToken().sign_rs256()};
    // mock_trf_client is expected to be called twice
    // 1. First time when access_token is empty
    // 2. When token is set to be expired
    EXPECT_CALL(mock_trf_client, request_with_grant_token()).Times(2);
    ON_CALL(mock_trf_client, request_with_grant_token())
        .WillByDefault(
            testing::Invoke([&mock_trf_client, &raw_token]() { mock_trf_client.set_token(raw_token, "Bearer"); }));

    const cpr::Url url{"http://127.0.0.1:12345/api/v1/sayHello"};
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    auto resp{cpr::Post(url, cpr::Header{{"Authorization", fmt::format("Bearer {}", mock_trf_client.get_token())}})};
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(cpr::status::HTTP_OK, resp.status_code);

    // use the acces_token saved from the previous call
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    resp = cpr::Post(url, cpr::Header{{"Authorization", mock_trf_client.get_typed_token()}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(cpr::status::HTTP_OK, resp.status_code);

    // set token to be expired invoking request_with_grant_token
    mock_trf_client.set_expiry(std::chrono::system_clock::now() - std::chrono::seconds(100));
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    resp = cpr::Post(url, cpr::Header{{"Authorization", mock_trf_client.get_typed_token()}});
    EXPECT_FALSE(resp.error);
    EXPECT_EQ(cpr::status::HTTP_OK, resp.status_code);
}

// Test request_with_grant_token. Setup http server with path /token to return token json
class TrfClientTest : public ::testing::Test {
public:
    TrfClientTest() = default;
    TrfClientTest(const TrfClientTest&) = delete;
    TrfClientTest& operator=(const TrfClientTest&) = delete;
    TrfClientTest(TrfClientTest&&) noexcept = delete;
    TrfClientTest& operator=(TrfClientTest&&) noexcept = delete;
    virtual ~TrfClientTest() override = default;

    virtual void SetUp() override {
        cfg.is_tls_enabled = false;
        cfg.bind_address = "127.0.0.1";
        cfg.server_port = 12345;
        cfg.read_write_timeout_secs = 10;
        cfg.is_auth_enabled = false;
        mock_server = std::unique_ptr< HttpServer >(
            new HttpServer(cfg, {handler_info("/token", TrfClientTest::get_token, this)}));
        mock_server->start();
    }

    virtual void TearDown() override { mock_server->stop(); }

    static void get_token(HttpCallData cd) {
        std::string msg;
        if (const auto r{pThis(cd)->mock_server->http_auth_verify(cd->request(), msg)}; r != EVHTP_RES_OK) {
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
    load_trf_settings();
    MockTrfClient mock_trf_client;
    EXPECT_CALL(mock_trf_client, request_with_grant_token()).Times(1);
    ON_CALL(mock_trf_client, request_with_grant_token()).WillByDefault(testing::Invoke([&mock_trf_client]() {
        mock_trf_client.__request_with_grant_token();
    }));
    remove_grant_path();
    EXPECT_THROW(
        {
            try {
                mock_trf_client.get_token();
            } catch (const std::runtime_error& e) {
                EXPECT_EQ(
                    e.what(),
                    fmt::format("could not load grant from path {}", SECURITY_DYNAMIC_CONFIG(trf_client->grant_path)));
                throw e;
            }
        },
        std::runtime_error);
}

TEST_F(TrfClientTest, request_with_grant_token) {
    load_trf_settings();
    MockTrfClient mock_trf_client;
    const auto raw_token{TestToken().sign_rs256()};
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
*/

using namespace sisl;
using namespace sisl::testing;

int main(int argc, char* argv[]) {
    ::testing::InitGoogleMock(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging)
    return RUN_ALL_TESTS();
}
