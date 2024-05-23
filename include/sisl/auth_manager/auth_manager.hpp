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
#include "LRUCache.h"

namespace sisl {

ENUM(AuthVerifyStatus, uint8_t, OK, UNAUTH, FORBIDDEN)

template < typename key_t, typename value_t >
class LRUCache;

/**
 * This struct holds information of a token, that can be used as if
 * they were extracted from decoded token.
 */
struct CachedToken {
    AuthVerifyStatus response_status;
    std::string msg;
    bool valid;
    std::chrono::system_clock::time_point expires_at;

    inline void set_invalid(AuthVerifyStatus code, const std::string& reason) {
        valid = false;
        response_status = code;
        msg = reason;
    }

    inline void set_valid() {
        valid = true;
        response_status = AuthVerifyStatus::OK;
    }
};

class AuthManager {
public:
    AuthManager();
    virtual ~AuthManager() = default;
    AuthVerifyStatus verify(const std::string& token, std::string& msg) const;

private:
    void verify_decoded(const jwt::decoded_jwt& decoded) const;
    virtual std::string download_key(const std::string& key_url) const;
    std::string get_app(const jwt::decoded_jwt& decoded) const;

    // key_id -> signing public key
    mutable LRUCache< std::string, std::string > m_cached_keys;
};
} // namespace sisl
