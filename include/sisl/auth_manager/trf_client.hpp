#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#undef HTTP_OK // nameclash with cpr/cpr.h header
#include <cpr/cpr.h>
#include <fmt/format.h>
#include "security_config.hpp"

namespace sisl {

class TrfClient {
public:
    TrfClient();
    std::string get_token();
    std::string get_token_type();
    std::string get_typed_token() {
        // get_token needs to be called first which might potentially set token type
        const auto token{get_token()};
        return fmt::format("{} {}", get_token_type(), token);
    }

private:
    void validate_grant_path() const;
    bool grant_path_exists() const { return std::filesystem::exists(SECURITY_DYNAMIC_CONFIG(trf_client->grant_path)); }
    bool access_token_expired() const {
        return (std::chrono::system_clock::now() >
                m_expiry + std::chrono::seconds(SECURITY_DYNAMIC_CONFIG(auth_manager->leeway)));
    }
    static bool get_file_contents(const std::string& file_name, std::string& contents);

private:
    std::shared_mutex m_mtx;

protected:
    // acquire unique lock before calling
    virtual void request_with_grant_token();
    void parse_response(const std::string& resp);
    static std::string get_string(const std::string& resp, const std::string& pattern);
    static std::string get_quoted_string(const std::string& resp, const std::string& pattern);

protected:
    std::string m_access_token;
    std::string m_token_type;
    std::chrono::system_clock::time_point m_expiry;
};

} // namespace sisl
