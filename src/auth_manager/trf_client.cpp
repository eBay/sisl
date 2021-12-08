#include "trf_client.hpp"
#include <chrono>
#include <cpr/payload.h>
#include <nlohmann/json.hpp>

namespace sisl {
TrfClient::TrfClient(const TrfClientConfig& cfg) : m_cfg(cfg) {
    uint8_t retry_limit{10};
    // Retry until the grant path is up. Might take few seconds when deployed as tess secret
    while (!grant_path_exists() && retry_limit-- > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!grant_path_exists()) {
        throw std::runtime_error(fmt::format("trustfabric client grant path {} does not exist", m_cfg.grant_path));
    }
}

bool TrfClient::get_file_contents(const std::string& file_path, std::string& contents) {
    try {
        std::ifstream f(file_path);
        std::string buffer(std::istreambuf_iterator< char >{f}, std::istreambuf_iterator< char >{});
        contents = buffer;
        return !contents.empty();
    } catch (...) {}
    return false;
}

void TrfClient::request_with_grant_token() {
    std::string grant_token;
    if (!get_file_contents(m_cfg.grant_path, grant_token)) {
        throw std::runtime_error(fmt::format("could not load grant from path {}", m_cfg.grant_path));
    }

    const auto client_id =
        fmt::format("ou={}+l={},o={},dc=tess,dc=ebay,dc=com", m_cfg.app_inst_name, m_cfg.app_env, m_cfg.app_name);

    cpr::Session session;
    if (m_cfg.verify) {
        auto ca_file = m_cfg.ssl_ca_file;
        auto cert_file = m_cfg.ssl_cert_file;
        auto key_file = m_cfg.ssl_key_file;
        auto sslOpts = cpr::Ssl(cpr::ssl::CaInfo{std::move(ca_file)});
        sslOpts.SetOption(cpr::ssl::CertFile{std::move(cert_file)});
        sslOpts.SetOption(cpr::ssl::KeyFile{std::move(key_file)});
        session.SetOption(sslOpts);
    }

    session.SetUrl(cpr::Url{m_cfg.server});
    std::vector< cpr::Pair > payload_data;
    payload_data.emplace_back("grant_type", "authorization_code");
    payload_data.emplace_back("code", grant_token);
    payload_data.emplace_back("client_id", client_id);
    session.SetPayload(cpr::Payload(payload_data.begin(), payload_data.end()));
    auto resp = session.Post();
    if (resp.error || resp.status_code != 200) {
        // TODO: log error, rest call failed
        return;
    }
    nlohmann::json resp_json;
    try {
        resp_json = nlohmann::json::parse(resp.text);
        std::string expires_in{resp_json["expires_in"]};
        m_expiry = std::chrono::system_clock::now() + std::chrono::seconds(std::stoi(expires_in));
        m_access_token = resp_json["access_token"];
        m_token_type = resp_json["token_type"];
    } catch (nlohmann::detail::exception& e) {
        // TODO: log error, parsing failed
        return;
    }
}

std::string TrfClient::get_token() {
    if (m_access_token.empty() || access_token_expired()) { request_with_grant_token(); }
    return m_access_token;
}
} // namespace sisl