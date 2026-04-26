/*********************************************************************************
 * Modifications Copyright 2026 eBay Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <httplib/httplib.h>

#include <sisl/auth_manager/token_verifier.hpp>
#include <sisl/utility/enum.hpp>

namespace sisl {

ENUM(url_type, uint8_t,
     localhost, // url can only be called from localhost
     safe,      // can be called from any host without auth
     regular);  // subject to normal auth rules

enum class http_method : uint8_t { Get, Post, Put, Delete, Patch, Head, Options };

using http_handler = httplib::Server::Handler;

struct http_route {
    http_method method;
    std::string resource;
    http_handler handler;
    sisl::url_type type{sisl::url_type::regular};
};

class HttpServer {
public:
    explicit HttpServer(uint16_t port = 5000, uint32_t num_threads = 1, uint64_t max_request_size = 4000000,
                        sisl::TokenVerifier* token_verifier = nullptr);
    HttpServer(std::string const& ssl_cert, std::string const& ssl_key, uint16_t port = 5000, uint32_t num_threads = 1,
               uint64_t max_request_size = 4000000, sisl::TokenVerifier* token_verifier = nullptr);
    ~HttpServer();

    // All routes must be registered before start()
    void setup_route(http_method method, std::string resource, http_handler handler,
                     url_type const& type = url_type::regular);
    void setup_routes(std::vector< http_route > const& routes);

    void start();
    void stop();
    void restart(std::string const& ssl_cert, std::string const& ssl_key);

#ifdef PROMETHEUS_METRICS_REPORTER
    // Registers GET /metrics → MetricsFarm::report(kTextFormat). Call before start().
    void register_metrics_endpoint();
#endif

    bool is_localaddr_url(std::string const& url) const;
    bool is_safe_url(std::string const& url) const;
    bool is_secure_zone() const;

private:
    void init();
    void get_local_ips();
    bool is_local_addr(std::string const& addr) const;
    void setup_route(http_route const& route, bool restart);
    bool do_auth(httplib::Request const& req, httplib::Response& res);
    bool auth_verify(httplib::Request const& req, httplib::Response& res) const;

private:
    uint16_t m_port;
    uint32_t m_num_threads;
    uint64_t m_max_request_size;
    sisl::TokenVerifier* m_token_verifier;
    std::string m_ssl_cert;
    std::string m_ssl_key;
    bool m_secure_zone{false};

    std::unique_ptr< httplib::Server > m_server;
    std::thread m_server_thread;
    std::atomic< bool > m_server_running{false};
    std::unordered_set< std::string > m_safelist;
    std::unordered_set< std::string > m_localhost_list;
    std::unordered_set< std::string > m_local_ips;
    std::vector< http_route > m_http_routes;
    std::mutex m_mutex;
};

} // namespace sisl
