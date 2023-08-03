#include <chrono>
#include <stdexcept>

#include <fmt/format.h>
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

#include "sisl/auth_manager/auth_manager.hpp"

namespace sisl {

AuthVerifyStatus AuthManager::verify(const std::string& token, std::string& msg) const {
    // TODO: cache tokens for better performance
    try {
        // this may throw if token is ill formed
        const auto decoded{jwt::decode(token)};

        // for any reason that causes the verification failure, an
        // exception is thrown.
        verify_decoded(decoded);
    } catch (const std::exception& e) {
        msg = e.what();
        return AuthVerifyStatus::UNAUTH;
    }

    // check client application

    if (SECURITY_DYNAMIC_CONFIG(auth_allowed_apps) != "all") {
        msg = fmt::format("any configuration other than [all] is not supported");
        return AuthVerifyStatus::FORBIDDEN;
    }

    return AuthVerifyStatus::OK;
}
void AuthManager::verify_decoded(const jwt::decoded_jwt& decoded) const {
    const auto alg{decoded.get_algorithm()};
    if (alg != "RS256") throw std::runtime_error(fmt::format("unsupported algorithm: {}", alg));

    if (!decoded.has_header_claim("x5u")) throw std::runtime_error("no indication of verification key");

    auto key_url = decoded.get_header_claim("x5u").as_string();

    if (key_url.rfind(SECURITY_DYNAMIC_CONFIG(tf_token_url), 0) != 0) {
        throw std::runtime_error(fmt::format("key url {} is not trusted", key_url));
    }
    const std::string signing_key{download_key(key_url)};
    const auto verifier{jwt::verify()
                            .with_issuer(SECURITY_DYNAMIC_CONFIG(issuer))
                            .allow_algorithm(jwt::algorithm::rs256(signing_key))
                            .expires_at_leeway(SECURITY_DYNAMIC_CONFIG(leeway))};

    // if verification fails, an instance of std::system_error subclass is thrown.
    verifier.verify(decoded);
}

std::string AuthManager::download_key(const std::string& key_url) const {
    cpr::Session session;
    session.SetUrl(cpr::Url{key_url});
    if (SECURITY_DYNAMIC_CONFIG(verify)) {
        auto ca_file{SECURITY_DYNAMIC_CONFIG(ssl_ca_file)};
        auto cert_file{SECURITY_DYNAMIC_CONFIG(ssl_cert_file)};
        auto key_file{SECURITY_DYNAMIC_CONFIG(ssl_key_file)};

        // constructor for CaInfo does std::move(filename)
        auto sslOpts{cpr::Ssl(cpr::ssl::CaInfo{std::move(ca_file)})};
        sslOpts.SetOption(cpr::ssl::CertFile{std::move(cert_file)});
        sslOpts.SetOption(cpr::ssl::KeyFile{std::move(key_file)});
        session.SetOption(sslOpts);
    }

    session.SetTimeout(std::chrono::milliseconds{5000});
    const auto resp{session.Get()};

    if (resp.error) { throw std::runtime_error(fmt::format("download key failed: {}", resp.error.message)); }
    if (resp.status_code != 200) { throw std::runtime_error(fmt::format("download key failed: {}", resp.text)); }

    return resp.text;
}
} // namespace sisl
