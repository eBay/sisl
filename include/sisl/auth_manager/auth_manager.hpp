#pragma once

#include <cstdint>
#include <string>

#include <sisl/utility/enum.hpp>
#include "security_config.hpp"

namespace jwt {
class decoded_jwt;
}

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
};
} // namespace sisl
