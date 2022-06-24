#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#undef HTTP_OK // nameclash with cpr/cpr.h header
#include <cpr/cpr.h>
#include <fmt/format.h>

namespace sisl {
struct TrfClientConfig {
    std::string app_name;
    std::string app_inst_name;
    std::string app_env;
    std::string pod_name;
    std::string server;
    uint32_t leeway;
    std::string grant_path;
    bool verify;
    std::string ssl_ca_file;
    std::string ssl_cert_file;
    std::string ssl_key_file;
};

class TrfClient {
public:
    TrfClient();
    TrfClient(const TrfClientConfig& cfg);
    std::string get_token();
    std::string get_typed_token() {
        const auto token_str{get_token()};
        return fmt::format("{} {}", m_token_type, token_str);
    }

private:
    void validate_grant_path();
    bool grant_path_exists() { return std::filesystem::exists(m_cfg.grant_path); }
    bool access_token_expired() {
        return (std::chrono::system_clock::now() > m_expiry + std::chrono::seconds(m_cfg.leeway));
    }
    static bool get_file_contents(const std::string& file_name, std::string& contents);

protected:
    virtual void request_with_grant_token();

protected:
    std::string m_access_token;
    std::string m_token_type;
    std::chrono::system_clock::time_point m_expiry;
    TrfClientConfig m_cfg;
};

} // namespace sisl
