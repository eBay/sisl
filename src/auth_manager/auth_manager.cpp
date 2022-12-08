#include <chrono>
#include <stdexcept>

#include <fmt/format.h>

#include "sisl/auth_manager/auth_manager.hpp"

namespace sisl {

AuthVerifyStatus AuthManager::verify(const std::string& token, std::string& msg) const {
    std::string app_name;
    // TODO: cache tokens for better performance
    try {
        // this may throw if token is ill formed
        const auto decoded{jwt::decode(token)};

        // for any reason that causes the verification failure, an
        // exception is thrown.
        verify_decoded(decoded);
        app_name = get_app(decoded);
    } catch (const std::exception& e) {
        msg = e.what();
        return AuthVerifyStatus::UNAUTH;
    }

    // check client application

    if (SECURITY_DYNAMIC_CONFIG(auth_manager->auth_allowed_apps) != "all") {
        if (SECURITY_DYNAMIC_CONFIG(auth_manager->auth_allowed_apps).find(app_name) == std::string::npos) {
            msg = fmt::format("application '{}' is not allowed to perform the request", app_name);
            return AuthVerifyStatus::FORBIDDEN;
        }
    }

    return AuthVerifyStatus::OK;
}
void AuthManager::verify_decoded(const jwt::decoded_jwt& decoded) const {
    const auto alg{decoded.get_algorithm()};
    if (alg != "RS256") throw std::runtime_error(fmt::format("unsupported algorithm: {}", alg));

    if (!decoded.has_header_claim("x5u")) throw std::runtime_error("no indication of verification key");

    auto key_url = decoded.get_header_claim("x5u").as_string();

    if (key_url.rfind(SECURITY_DYNAMIC_CONFIG(auth_manager->tf_token_url), 0) != 0) {
        throw std::runtime_error(fmt::format("key url {} is not trusted", key_url));
    }
    const std::string signing_key{download_key(key_url)};
    const auto verifier{jwt::verify()
                            .with_issuer(SECURITY_DYNAMIC_CONFIG(auth_manager->issuer))
                            .allow_algorithm(jwt::algorithm::rs256(signing_key))
                            .expires_at_leeway(SECURITY_DYNAMIC_CONFIG(auth_manager->leeway))};

    // if verification fails, an instance of std::system_error subclass is thrown.
    verifier.verify(decoded);
}

std::string AuthManager::download_key(const std::string& key_url) const {
    cpr::Session session;
    session.SetUrl(cpr::Url{key_url});
    if (SECURITY_DYNAMIC_CONFIG(auth_manager->verify)) {
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

std::string AuthManager::get_app(const jwt::decoded_jwt& decoded) const {
    // get app name from client_id, which is the "sub" field in the decoded token
    // body
    // https://pages.github.corp.ebay.com/security-platform/documents/tf-documentation/tessintegration/#environment-variables
    if (!decoded.has_payload_claim("sub")) return "";

    const auto client_id{decoded.get_payload_claim("sub").as_string()};
    const auto start{client_id.find(",o=") + 3};
    const auto end{client_id.find_first_of(",", start)};
    return client_id.substr(start, end - start);
}
} // namespace sisl
