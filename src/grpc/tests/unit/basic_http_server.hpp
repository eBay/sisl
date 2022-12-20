/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
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
#include <pistache/endpoint.h>
#include <pistache/http.h>
#include <pistache/router.h>
#include <pistache/http_headers.h>
#include <string>
#include <nlohmann/json.hpp>

#pragma once

class APIBase {
public:
    static void init(Pistache::Address addr, size_t thr) {
        m_http_endpoint = std::make_shared< Pistache::Http::Endpoint >(addr);
        auto flags = Pistache::Tcp::Options::ReuseAddr;
        auto opts = Pistache::Http::Endpoint::options().threadsName("http_server").threads(thr).flags(flags);
        m_http_endpoint->init(opts);
    }

    static void start() {
        m_http_endpoint->setHandler(m_router.handler());
        m_http_endpoint->serveThreaded();
    }

    static void stop() { m_http_endpoint->shutdown(); }

    virtual ~APIBase() {}

protected:
    static std::shared_ptr< Pistache::Http::Endpoint > m_http_endpoint;
    static Pistache::Rest::Router m_router;
};

std::shared_ptr< Pistache::Http::Endpoint > APIBase::m_http_endpoint;
Pistache::Rest::Router APIBase::m_router;

class TokenApi : public APIBase {
public:
    void setupRoutes() {
        Pistache::Rest::Routes::Post(m_router, "/token",
                                     Pistache::Rest::Routes::bind(&TokenApi::get_token_handler, this));
        Pistache::Rest::Routes::Get(m_router, "/download_key",
                                    Pistache::Rest::Routes::bind(&TokenApi::get_key_handler, this));
    }

    void get_token_handler(const Pistache::Rest::Request&, Pistache::Http::ResponseWriter response) {
        this->get_token_impl(response);
    }

    void get_key_handler(const Pistache::Rest::Request&, Pistache::Http::ResponseWriter response) {

        this->get_key_impl(response);
    }

    virtual void get_token_impl(Pistache::Http::ResponseWriter& response) = 0;
    virtual void get_key_impl(Pistache::Http::ResponseWriter& response) = 0;

    virtual ~TokenApi() {
        Pistache::Rest::Routes::Remove(m_router, Pistache::Http::Method::Post, "/token");
        Pistache::Rest::Routes::Remove(m_router, Pistache::Http::Method::Get, "/download_key");
    }
};
