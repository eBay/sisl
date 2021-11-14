#pragma once

#include <cpr/cpr.h>
#include "utility/enum.hpp"
#include <string>
// maybe-uninitialized variable in one of the included headers from jwt.h
#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <jwt-cpp/jwt.h>
#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic pop
#endif

namespace sisl {

struct AuthMgrConfig {
    std::string tf_token_url;
    std::string ssl_cert_file;
    std::string ssl_key_file;
    std::string ssl_ca_file;
    uint32_t auth_exp_leeway;
    std::string auth_allowed_apps;
};

ENUM(AuthVerifyStatus, uint8_t, OK, UNAUTH, FORBIDDEN)

class AuthManager {
public:
    AuthManager() = default;
    AuthManager(const AuthMgrConfig& cfg) : m_cfg(cfg) {}
    ~AuthManager() = default;
    void set_config(const AuthMgrConfig& cfg) { m_cfg = cfg; }
    AuthVerifyStatus verify(const std::string& token, std::string& msg);
    // for testing
    void set_allowed_to_all() { m_cfg.auth_allowed_apps = "all"; }

private:
    void verify_decoded(const jwt::decoded_jwt& decoded);
    virtual std::string download_key(const std::string& key_url);

private:
    AuthMgrConfig m_cfg;
    std::string get_app(const jwt::decoded_jwt& decoded);
};
} // namespace sisl