#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sisl/auth_manager/auth_manager.hpp"
#include "sisl/auth_manager/trf_client.hpp"
#include "test_token.hpp"
#include "basic_http_server.hpp"

SISL_OPTIONS_ENABLE(logging)

namespace sisl::testing {
using namespace ::testing;

static std::string get_cur_file_dir() {
    const std::string cur_file_path{__FILE__};
    const auto last_slash_pos{cur_file_path.rfind('/')};
    if (last_slash_pos == std::string::npos) { return ""; }
    return std::string{cur_file_path.substr(0, last_slash_pos + 1)};
}

static const std::string cur_file_dir{get_cur_file_dir()};

static const std::string grant_path = fmt::format("{}/dummy_grant.cg", cur_file_dir);

class MockAuthManager : public AuthManager {
public:
    using AuthManager::AuthManager;
    MOCK_METHOD(std::string, download_key, (const std::string&), (const));
    AuthVerifyStatus verify(const std::string& token) {
        std::string msg;
        return AuthManager::verify(token, msg);
    }
};

class AuthTest : public ::testing::Test {
public:
    virtual void SetUp() override {
        load_settings();
        mock_auth_mgr = std::shared_ptr< MockAuthManager >(new MockAuthManager());
    }

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
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(0);
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
    EXPECT_EQ(mock_auth_mgr->verify(token.sign_rs256()), AuthVerifyStatus::UNAUTH);
}

TEST_F(AuthTest, reject_untrusted_keyurl) {
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(0);
    // the key url is an untrusted address, we only trust "http://127.0.0.1"
    auto token{TestToken()};
    token.get_token().set_header_claim("x5u", jwt::claim(std::string{"http://untrusted.addr/keys/abc123"}));
    EXPECT_EQ(mock_auth_mgr->verify(token.sign_rs256()), AuthVerifyStatus::UNAUTH);
}

TEST_F(AuthTest, reject_expired_token) {
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    // token expired 1 second ago
    auto token{TestToken()};
    token.get_token().set_expires_at(std::chrono::system_clock::now() - std::chrono::seconds(1));
    EXPECT_EQ(mock_auth_mgr->verify(token.sign_rs256()), AuthVerifyStatus::UNAUTH);
}

TEST_F(AuthTest, reject_download_key_fail) {
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Throw(std::runtime_error("download key failed")));
    EXPECT_EQ(mock_auth_mgr->verify(TestToken().sign_rs512()), AuthVerifyStatus::UNAUTH);
}

TEST_F(AuthTest, reject_wrong_key) {
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub1_key));
    EXPECT_EQ(mock_auth_mgr->verify(TestToken().sign_rs512()), AuthVerifyStatus::UNAUTH);
}

TEST_F(AuthTest, allow_all_apps) {
    set_allowed_to_all();
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    auto token{TestToken()};
    token.get_token().set_subject("any-prefix,o=dummy_app,dc=tess,dc=ebay,dc=com");
    EXPECT_EQ(mock_auth_mgr->verify(token.sign_rs256()), AuthVerifyStatus::OK);
}

TEST_F(AuthTest, reject_unauthorized_app) {
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    // the client application is "myapp", which is not in the allowed list
    auto token{TestToken()};
    token.get_token().set_subject("any-prefix,o=myapp,dc=tess,dc=ebay,dc=com");
    EXPECT_EQ(mock_auth_mgr->verify(token.sign_rs256()), AuthVerifyStatus::FORBIDDEN);
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
    std::chrono::system_clock::time_point get_expiry() { return m_expiry; }

    void parse_token(const std::string& resp) { TrfClient::parse_response(resp); }
};

static void load_trf_settings() {
    std::ofstream outfile{grant_path};
    outfile << "dummy cg contents\n";
    outfile.close();
    SECURITY_SETTINGS_FACTORY().modifiable_settings([](auto& s) {
        s.trf_client->grant_path = grant_path;
        s.trf_client->server = "127.0.0.1:12346/token";
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

    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(1).WillOnce(Return(rsa_pub_key));
    EXPECT_EQ(mock_auth_mgr->verify(mock_trf_client.get_token()), AuthVerifyStatus::OK);

    // use the acces_token saved from the previous call
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(0);
    EXPECT_EQ(mock_auth_mgr->verify(mock_trf_client.get_token()), AuthVerifyStatus::OK);

    // set token to be expired invoking request_with_grant_token
    mock_trf_client.set_expiry(std::chrono::system_clock::now() - std::chrono::seconds(100));
    EXPECT_CALL(*mock_auth_mgr, download_key(_)).Times(0);
    EXPECT_EQ(mock_auth_mgr->verify(mock_trf_client.get_token()), AuthVerifyStatus::OK);
}

static const std::string trf_token_server_ip{"127.0.0.1"};
static const uint32_t trf_token_server_port{12346};
static std::string token_response;
static void set_token_response(const std::string& raw_token) {
    token_response = "{\"access_token\":\"" + raw_token +
        "\",\"token_type\":\"Bearer\",\"expires_in\":2000,\"refresh_token\":\"dummy_refresh_token\"}\n";
}

class TokenApiImpl : public TokenApi {
public:
    void get_token_impl(Pistache::Http::ResponseWriter& response) {
        LOGINFO("Sending token to client");
        response.send(Pistache::Http::Code::Ok, token_response);
    }
};

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
        // start token server
        APIBase::init(Pistache::Address(fmt::format("{}:{}", trf_token_server_ip, trf_token_server_port)), 1);
        m_token_server = std::unique_ptr< TokenApiImpl >(new TokenApiImpl());
        m_token_server->setupRoutes();
        APIBase::start();
    }

    virtual void TearDown() override {
        APIBase::stop();
        remove_grant_path();
    }

private:
    std::unique_ptr< TokenApiImpl > m_token_server;
};

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
    set_token_response(raw_token);
    EXPECT_CALL(mock_trf_client, request_with_grant_token()).Times(1);
    ON_CALL(mock_trf_client, request_with_grant_token()).WillByDefault(testing::Invoke([&mock_trf_client]() {
        mock_trf_client.__request_with_grant_token();
    }));
    mock_trf_client.get_token();
    EXPECT_EQ(raw_token, mock_trf_client.get_access_token());
    EXPECT_EQ("Bearer", mock_trf_client.get_token_type());
}

TEST(TrfClientParseTest, parse_token) {
    load_trf_settings();
    MockTrfClient mock_trf_client;
    const auto raw_token{TestToken().sign_rs256()};
    set_token_response(raw_token);
    EXPECT_TRUE(mock_trf_client.get_access_token().empty());
    EXPECT_TRUE(mock_trf_client.get_token_type().empty());
    mock_trf_client.parse_token(token_response);
    EXPECT_EQ(raw_token, mock_trf_client.get_access_token());
    EXPECT_EQ("Bearer", mock_trf_client.get_token_type());
    EXPECT_TRUE(mock_trf_client.get_expiry() > std::chrono::system_clock::now());
    remove_grant_path();
}
} // namespace sisl::testing

using namespace sisl;
using namespace sisl::testing;

int main(int argc, char* argv[]) {
    ::testing::InitGoogleMock(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging)
    return RUN_ALL_TESTS();
}
