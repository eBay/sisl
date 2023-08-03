#pragma once

#include <cstdint>
#include <string>

#include <sisl/utility/enum.hpp>

namespace grpc {
class Status;
class ServerContext;
} // namespace grpc

namespace sisl {

// An interface for verifing a token for authorization. This can be used in conjunction with the TokenClient which is an
// interface to get a token. The implementation is deployment specific, one example is jwt based tokens provided by
// ebay/TrustFabric

ENUM(VerifyCode, uint8_t, OK, UNAUTH, FORBIDDEN)

struct TokenVerifyStatus {
    VerifyCode code;
    std::string msg;
};

class TokenVerifier {
public:
    virtual ~TokenVerifier() = default;
    virtual TokenVerifyStatus verify(std::string const& token) const = 0;
};

// extracts the key value pairs (m_auth_header_key, get_token()) from grpc client context and verifies the token
class GrpcTokenVerifier : public TokenVerifier {
public:
    explicit GrpcTokenVerifier(std::string const& auth_header_key) : m_auth_header_key(auth_header_key) {}
    virtual ~GrpcTokenVerifier() = default;

    virtual grpc::Status verify(grpc::ServerContext const* srv_ctx) const = 0;

protected:
    std::string m_auth_header_key;
};

} // namespace sisl
