#pragma once

#include <cstdint>
#include <string>

#undef HTTP_OK // nameclash with cpr/cpr.h header
#include <cpr/cpr.h>

// maybe-uninitialized variable in one of the included headers from jwt.h
#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <jwt-cpp/jwt.h>
#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic pop
#endif

#include <sisl/utility/enum.hpp>
#include "security_config.hpp"

namespace sisl {

ENUM(AuthVerifyStatus, uint8_t, OK, UNAUTH, FORBIDDEN)

class AuthManager {
public:
    AuthManager() {}
    virtual ~AuthManager() = default;
    AuthVerifyStatus verify(const std::string& token, std::string& msg) const;

private:
    void verify_decoded(const jwt::decoded_jwt& decoded) const;
    virtual std::string download_key(const std::string& key_url) const;
    std::string get_app(const jwt::decoded_jwt& decoded) const;
};
} // namespace sisl
