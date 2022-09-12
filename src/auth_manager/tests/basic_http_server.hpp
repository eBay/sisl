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
    }

    void get_token_handler(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
        this->get_token_impl(response);
    }

    virtual void get_token_impl(Pistache::Http::ResponseWriter& response) = 0;

    virtual ~TokenApi() { Pistache::Rest::Routes::Remove(m_router, Pistache::Http::Method::Post, "/token"); }
};