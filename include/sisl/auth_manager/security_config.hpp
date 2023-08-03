#pragma once
#include <sisl/options/options.h>
#include <sisl/settings/settings.hpp>
#include "generated/security_config_generated.h"

SETTINGS_INIT(securitycfg::SecuritySettings, security_config)

#define SECURITY_DYNAMIC_CONFIG_WITH(...) SETTINGS(security_config, __VA_ARGS__)
#define SECURITY_DYNAMIC_CONFIG_THIS(...) SETTINGS_THIS(security_config, __VA_ARGS__)
#define SECURITY_DYNAMIC_CONFIG(...) SETTINGS_VALUE(security_config, __VA_ARGS__)

#define SECURITY_SETTINGS_FACTORY() SETTINGS_FACTORY(security_config)

class SecurityDynamicConfig {
public:
    static std::string get_env(const std::string& env_str) {
        auto env_var = getenv(env_str.c_str());
        return (env_var != nullptr) ? std::string(env_var) : "";
    }
    inline static const std::string default_auth_allowed_apps{"all"};
    inline static const std::string default_ssl_cert_file{get_env("SSL_CERT")};
    inline static const std::string default_ssl_key_file{get_env("SSL_KEY")};
    inline static const std::string default_ssl_ca_file{get_env("SSL_CA")};
    inline static const std::string default_tf_token_url{get_env("TOKEN_URL")};
    inline static const std::string default_issuer{get_env("TOKEN_ISSUER")};

    // This method sets up the default for settings factory when there is no override specified in the json
    // file and .fbs cannot specify default because they are not scalar.
    static void init_settings_default() {
        bool is_modified = false;
        SECURITY_SETTINGS_FACTORY().modifiable_settings([&is_modified](auto& s) {
            auto& ssl_cert_file = s.ssl_cert_file;
            if (ssl_cert_file.empty()) {
                ssl_cert_file = default_ssl_cert_file;
                is_modified = true;
            }
            auto& ssl_key_file = s.ssl_key_file;
            if (ssl_key_file.empty()) {
                ssl_key_file = default_ssl_key_file;
                is_modified = true;
            }
            auto& ssl_ca_file = s.ssl_ca_file;
            if (ssl_ca_file.empty()) {
                ssl_ca_file = default_ssl_ca_file;
                is_modified = true;
            }
            auto& issuer = s.issuer;
            if (issuer.empty()) {
                issuer = default_issuer;
                is_modified = true;
            }
            auto& auth_allowed_apps = s.auth_allowed_apps;
            if (auth_allowed_apps.empty()) {
                auth_allowed_apps = default_auth_allowed_apps;
                is_modified = true;
            }
            auto& tf_token_url = s.tf_token_url;
            if (tf_token_url.empty()) {
                tf_token_url = default_tf_token_url;
                is_modified = true;
            }
            // Any more default overrides or set non-scalar entries come here
        });

        if (is_modified) { SECURITY_SETTINGS_FACTORY().save(); }
    }
};
