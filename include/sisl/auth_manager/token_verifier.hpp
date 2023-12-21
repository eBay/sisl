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

// This class represents the return value to the token verify call.
// Derive from this class if the return value needs to contain some information from the decoded token.
class TokenVerifyState {
public:
    TokenVerifyState() = default;
    TokenVerifyState(VerifyCode const c, std::string const& m) : code(c), msg(m) {}
    virtual ~TokenVerifyState() {}
    VerifyCode code;
    std::string msg;
};

using token_state_ptr = std::shared_ptr< TokenVerifyState >;

class TokenVerifier {
public:
    virtual ~TokenVerifier() = default;
    virtual token_state_ptr verify(std::string const& token) const = 0;
};

// extracts the key value pairs (m_auth_header_key, get_token()) from grpc client context and verifies the token
class GrpcTokenVerifier : public TokenVerifier {
public:
    explicit GrpcTokenVerifier(std::string const& auth_header_key) : m_auth_header_key(auth_header_key) {}
    virtual ~GrpcTokenVerifier() = default;

    using TokenVerifier::verify;
    virtual grpc::Status verify(grpc::ServerContext const* srv_ctx) const = 0;

protected:
    std::string m_auth_header_key;
};

} // namespace sisl
