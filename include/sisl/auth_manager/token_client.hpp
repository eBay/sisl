#pragma once
#include <string>

namespace sisl {

class TokenClient {
public:
    virtual ~TokenClient() = default;

    virtual std::string get_token() = 0;
};

// the key value pairs (m_auth_header_key, get_token()) are sent as metadata in the grpc client context

class GrpcTokenClient : public TokenClient {
public:
    explicit GrpcTokenClient(std::string const& auth_header_key) : m_auth_header_key(auth_header_key) {}
    virtual ~GrpcTokenClient() = default;

    std::string get_auth_header_key() const { return m_auth_header_key; }

private:
    std::string m_auth_header_key;
};
} // namespace sisl