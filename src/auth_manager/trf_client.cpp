#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <cpr/payload.h>
#include <nlohmann/json.hpp>

#include "trf_client.hpp"

namespace sisl {
TrfClient::TrfClient() { validate_grant_path(); }

void TrfClient::validate_grant_path() const {
    uint8_t retry_limit{10};
    // Retry until the grant path is up. Might take few seconds when deployed as tess secret
    while (!grant_path_exists() && retry_limit-- > 0) {
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }
    if (!grant_path_exists()) {
        throw std::runtime_error{fmt::format("trustfabric client grant path {} does not exist",
                                             SECURITY_DYNAMIC_CONFIG(trf_client->grant_path))};
    }
}

bool TrfClient::get_file_contents(const std::string& file_path, std::string& contents) {
    try {
        std::ifstream f{file_path};
        const std::string buffer{std::istreambuf_iterator< char >{f}, std::istreambuf_iterator< char >{}};
        contents = buffer;
        return !contents.empty();
    } catch (...) {}
    return false;
}

void TrfClient::request_with_grant_token() {
    std::string grant_token;
    if (!get_file_contents(SECURITY_DYNAMIC_CONFIG(trf_client->grant_path), grant_token)) {
        throw std::runtime_error(
            fmt::format("could not load grant from path {}", SECURITY_DYNAMIC_CONFIG(trf_client->grant_path)));
    }

    const auto client_id{
        fmt::format("ou={}+l={},o={},dc=tess,dc=ebay,dc=com", SECURITY_DYNAMIC_CONFIG(trf_client->app_inst_name),
                    SECURITY_DYNAMIC_CONFIG(trf_client->app_env), SECURITY_DYNAMIC_CONFIG(trf_client->app_name))};

    cpr::Session session;
    if (SECURITY_DYNAMIC_CONFIG(auth_manager->verify)) {
        auto ca_file{SECURITY_DYNAMIC_CONFIG(ssl_ca_file)};
        auto cert_file{SECURITY_DYNAMIC_CONFIG(ssl_cert_file)};
        auto key_file{SECURITY_DYNAMIC_CONFIG(ssl_key_file)};
        auto sslOpts{cpr::Ssl(cpr::ssl::CaInfo{std::move(ca_file)})};
        sslOpts.SetOption(cpr::ssl::CertFile{std::move(cert_file)});
        sslOpts.SetOption(cpr::ssl::KeyFile{std::move(key_file)});
        session.SetOption(sslOpts);
    }

    session.SetUrl(cpr::Url{SECURITY_DYNAMIC_CONFIG(trf_client->server)});
    std::vector< cpr::Pair > payload_data;
    payload_data.emplace_back("grant_type", "authorization_code");
    payload_data.emplace_back("code", grant_token);
    payload_data.emplace_back("client_id", client_id);
    session.SetPayload(cpr::Payload(payload_data.begin(), payload_data.end()));
    session.SetTimeout(std::chrono::milliseconds{5000});
    const auto resp{session.Post()};
    if (resp.error || resp.status_code != 200) {
        LOGDEBUG("request grant token from server failed, error: {}, status code: {}", resp.error.message,
                 resp.status_code);
        return;
    }

    try {
        const nlohmann::json resp_json = nlohmann::json::parse(resp.text);
        m_expiry = std::chrono::system_clock::now() + std::chrono::seconds(resp_json["expires_in"]);
        m_access_token = resp_json["access_token"];
        m_token_type = resp_json["token_type"];
    } catch ([[maybe_unused]] const nlohmann::detail::exception& e) {
        LOGDEBUG("parsing token response failed, what: {}", e.what());
    }
}

std::string TrfClient::get_token() {
    {
        std::shared_lock< std::shared_mutex > lock(m_mtx);
        if (!(m_access_token.empty() || access_token_expired())) { return m_access_token; }
    }

    // Not a frequent code path, occurs for the first time or when token expires
    std::unique_lock< std::shared_mutex > lock(m_mtx);
    request_with_grant_token();
    return m_access_token;
}

std::string TrfClient::get_token_type() {
    std::shared_lock< std::shared_mutex > lock(m_mtx);
    return m_token_type;
}
} // namespace sisl
