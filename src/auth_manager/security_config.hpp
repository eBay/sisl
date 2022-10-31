#pragma once
#include "../settings/settings.hpp"
#include "../options/options.h"
#include "generated/security_config_generated.h"

SETTINGS_INIT(securitycfg::SecuritySettings, security_config)

#define SECURITY_DYNAMIC_CONFIG_WITH(...) SETTINGS(security_config, __VA_ARGS__)
#define SECURITY_DYNAMIC_CONFIG_THIS(...) SETTINGS_THIS(security_config, __VA_ARGS__)
#define SECURITY_DYNAMIC_CONFIG(...) SETTINGS_VALUE(security_config, __VA_ARGS__)

#define SECURITY_SETTINGS_FACTORY() SETTINGS_FACTORY(security_config)

class SecurityDynamicConfig {
public:
    static constexpr std::string_view default_auth_allowed_apps = "all";

    static std::string get_env(const std::string& env_str) {
        auto env_var = getenv(env_str.c_str());
        return (env_var != nullptr) ? std::string(env_var) : "";
    }

    inline static const std::string default_app_name{get_env("APP_NAME")};
    inline static const std::string default_app_inst_name{get_env("APP_INST_NAME")};
    inline static const std::string default_pod_name{get_env("POD_NAME")};
    inline static const std::string default_app_env{get_env("APP_ENV")};
    inline static const std::string default_ssl_cert_file{get_env("SSL_CERT")};
    inline static const std::string default_ssl_key_file{get_env("SSL_KEY")};
    inline static const std::string default_ssl_ca_file{get_env("SSL_CA")};
    inline static const std::string default_tf_token_url{get_env("TOKEN_URL")};
    inline static const std::string default_issuer{get_env("TOKEN_ISSUER")};
    inline static const std::string default_server{get_env("TOKEN_SERVER")};
    inline static const std::string default_grant_path{get_env("TOKEN_GRANT")};

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
            auto& server = s.trf_client->server;
            if (server.empty()) {
                server = std::string_view(default_server);
                is_modified = true;
            }
            auto& grant_path = s.trf_client->grant_path;
            if (grant_path.empty()) {
                grant_path = std::string_view(default_grant_path);
                is_modified = true;
            }
            auto& auth_allowed_apps = s.auth_manager->auth_allowed_apps;
            if (auth_allowed_apps.empty()) {
                auth_allowed_apps = default_auth_allowed_apps;
                is_modified = true;
            }
            auto& issuer = s.auth_manager->issuer;
            if (issuer.empty()) {
                issuer = default_issuer;
                is_modified = true;
            }
            auto& tf_token_url = s.auth_manager->tf_token_url;
            if (tf_token_url.empty()) {
                tf_token_url = default_tf_token_url;
                is_modified = true;
            }
            auto& app_name = s.trf_client->app_name;
            if (app_name.empty()) {
                app_name = std::string_view(default_app_name);
                is_modified = true;
            }
            auto& app_inst_name = s.trf_client->app_inst_name;
            if (app_inst_name.empty()) {
                app_inst_name = std::string_view(default_app_inst_name);
                is_modified = true;
            }
            auto& app_env = s.trf_client->app_env;
            if (app_env.empty()) {
                app_env = std::string_view(default_app_env);
                is_modified = true;
            }
            auto& pod_name = s.trf_client->pod_name;
            if (pod_name.empty()) {
                pod_name = std::string_view(default_pod_name);
                is_modified = true;
            }

            // Any more default overrides or set non-scalar entries come here
        });

        if (is_modified) { SECURITY_SETTINGS_FACTORY().save(); }
    }
};
