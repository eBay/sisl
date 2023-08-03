#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sisl/auth_manager/auth_manager.hpp"
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
        SECURITY_SETTINGS_FACTORY().modifiable_settings([](auto& s) { s.auth_allowed_apps = "all"; });
        SECURITY_SETTINGS_FACTORY().save();
    }

    static void load_settings() {
        SECURITY_SETTINGS_FACTORY().modifiable_settings([](auto& s) {
            s.auth_allowed_apps = "all";
            s.tf_token_url = "http://127.0.0.1";
            s.leeway = 0;
            s.issuer = "trustfabric";
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
    // change allowed apps from all which is forbidden
    SECURITY_SETTINGS_FACTORY().modifiable_settings([](auto& s) { s.auth_allowed_apps = "dummy"; });
    SECURITY_SETTINGS_FACTORY().save();

    auto token{TestToken()};
    token.get_token().set_subject("any-prefix,o=myapp,dc=tess,dc=ebay,dc=com");
    EXPECT_EQ(mock_auth_mgr->verify(token.sign_rs256()), AuthVerifyStatus::FORBIDDEN);

    // reset back to all
    SECURITY_SETTINGS_FACTORY().modifiable_settings([](auto& s) { s.auth_allowed_apps = "all"; });
    SECURITY_SETTINGS_FACTORY().save();
}

} // namespace sisl::testing

int main(int argc, char* argv[]) {
    ::testing::InitGoogleMock(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging)
    return RUN_ALL_TESTS();
}
