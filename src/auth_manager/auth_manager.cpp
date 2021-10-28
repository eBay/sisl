
#include "auth_manager.hpp"

namespace sisl {
AuthVerifyStatus verify(const std::string& token) {
    std::string app_name;
    // TODO: cache tokens for better performance
    try {
        // this may throw if token is ill formed
        auto decoded = jwt::decode(token);

        // for any reason that causes the verification failure, an
        // exception is thrown.
        verify_decoded(decoded);
        app_name = get_app(decoded);
    } catch (const std::exception& e) {
        msg = e.what();
        LOGDEBUGMOD(httpserver_lmod, "Processing req={}; {}", (void*)req, e.what());
        return AuthVerifyStatus::UNAUTH;
    }

    // check client application

    if (m_cfg.auth_allowed_apps != "all") {
        if (m_cfg.auth_allowed_apps.find(app_name) == std::string::npos) {
            msg = fmt::format("application '{}' is not allowed to perform the request", app_name);
            LOGDEBUGMOD(httpserver_lmod, "Processing req={}; {}", (void*)req, msg);
            return AuthVerifyStatus::FORBIDDEN;
        }
    }

    return AuthVerifyStatus::OK;
}
void verify_decoded(const jwt::decoded_jwt& decoded) {
    auto alg = decoded.get_algorithm();
    if (alg != "RS256") throw std::runtime_error(fmt::format("unsupported algorithm: {}", alg));

    std::string signing_key;
    if (!decoded.has_header_claim("x5u")) throw std::runtime_error("no indication of verification key");

    auto key_url = decoded.get_header_claim("x5u").as_string();

    if (key_url.rfind(m_cfg.tf_token_url, 0) != 0) {
        throw std::runtime_error(fmt::format("key url {} is not trusted", key_url));
    }
    signing_key = download_key(key_url);
    auto verifier = jwt::verify()
                        .with_issuer("trustfabric")
                        .allow_algorithm(jwt::algorithm::rs256(signing_key))
                        .expires_at_leeway(m_cfg.auth_exp_leeway);

    // if verification fails, an instance of std::system_error subclass is thrown.
    verifier.verify(decoded);
}

virtual std::string download_key(const std::string& key_url) {
    auto ca_file = m_cfg.ssl_ca_file;
    auto cert_file = m_cfg.ssl_cert_file;
    auto key_file = m_cfg.ssl_key_file;

    // constructor for CaInfo does std::move(filename)
    auto sslOpts = cpr::Ssl(cpr::ssl::CaInfo{std::move(ca_file)});
    sslOpts.SetOption(cpr::ssl::CertFile{std::move(cert_file)});
    sslOpts.SetOption(cpr::ssl::KeyFile{std::move(key_file)});

    cpr::Session session;
    session.SetUrl(cpr::Url{key_url});
    session.SetOption(sslOpts);

    auto resp = session.Get();

    if (resp.error) { throw std::runtime_error(fmt::format("download key failed: {}", resp.error.message)); }
    if (resp.status_code != 200) { throw std::runtime_error(fmt::format("download key failed: {}", resp.text)); }

    return resp.text;
}

std::string get_app(const jwt::decoded_jwt& decoded) {
    // get app name from client_id, which is the "sub" field in the decoded token
    // body
    // https://pages.github.corp.ebay.com/security-platform/documents/tf-documentation/tessintegration/#environment-variables
    if (!decoded.has_payload_claim("sub")) return "";

    auto client_id = decoded.get_payload_claim("sub").as_string();
    auto start = client_id.find(",o=") + 3;
    auto end = client_id.find_first_of(",", start);
    return client_id.substr(start, end - start);
}
} // namespace sisl