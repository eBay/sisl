#include <chrono>
#include <filesystem>
#include <thread>

#include <arpa/inet.h>
#include <ifaddrs.h>

#include <sisl/http/http_server.hpp>
#include <sisl/logging/logging.h>

#ifdef PROMETHEUS_METRICS_REPORTER
#include <sisl/metrics/metrics.hpp>
#endif

SISL_LOGGING_DECL(http)

namespace sisl {

static Pistache::Http::Code to_pistache_code(sisl::token_state_ptr const status) {
    switch (status->code) {
    case sisl::VerifyCode::OK:
        return Pistache::Http::Code::Ok;
    case sisl::VerifyCode::UNAUTH:
        return Pistache::Http::Code::Unauthorized;
    case sisl::VerifyCode::FORBIDDEN:
        return Pistache::Http::Code::Forbidden;
    default:
        break;
    }
    return Pistache::Http::Code::Precondition_Failed;
}

HttpServer::HttpServer(uint16_t port, uint32_t num_threads, uint64_t max_request_size,
                       sisl::TokenVerifier* token_verifier) :
        m_port{port},
        m_num_threads{num_threads},
        m_max_request_size{max_request_size},
        m_token_verifier{token_verifier} {
    init();
    get_local_ips();
}

HttpServer::HttpServer(std::string const& ssl_cert, std::string const& ssl_key, uint16_t port, uint32_t num_threads,
                       uint64_t max_request_size, sisl::TokenVerifier* token_verifier) :
        m_port{port},
        m_num_threads{num_threads},
        m_max_request_size{max_request_size},
        m_token_verifier{token_verifier},
        m_ssl_cert{ssl_cert},
        m_ssl_key{ssl_key},
        m_secure_zone{!ssl_cert.empty() && !ssl_key.empty()} {
    if (!m_secure_zone && (!ssl_cert.empty() || !ssl_key.empty())) {
        LOGERROR("one of ssl_cert {}, ssl_key: {} is empty!", ssl_cert, ssl_key);
        return;
    }
    init();
    get_local_ips();
}

void HttpServer::init() {
    m_http_endpoint.reset();
    Pistache::Address addr(Pistache::Ipv4::any(), Pistache::Port(m_port));
    m_http_endpoint = std::make_unique< Pistache::Http::Endpoint >(addr);
    auto flags = Pistache::Tcp::Options::ReuseAddr;
    auto opts = Pistache::Http::Endpoint::options()
                    .threadsName("http_server")
                    .maxRequestSize(m_max_request_size)
                    .threads(m_num_threads)
                    .flags(flags);
    m_http_endpoint->init(opts);
    setup_ssl(m_ssl_cert, m_ssl_key);
}

void HttpServer::start() {
    m_router.addMiddleware(Pistache::Rest::Routes::middleware(&HttpServer::do_auth, this));
    m_http_endpoint->setHandler(m_router.handler());
    m_http_endpoint->serveThreaded();
    m_server_running = true;
}

void HttpServer::stop() {
    m_http_endpoint->shutdown();
    m_server_running = false;
}

void HttpServer::restart(std::string const& ssl_cert, std::string const& ssl_key) {
    std::unique_lock< std::mutex > lock(m_mutex);
    m_server_running = false;
    m_ssl_cert = ssl_cert;
    m_ssl_key = ssl_key;
    m_secure_zone = !ssl_cert.empty() && !ssl_key.empty();
    init();
    m_localhost_list.clear();
    m_safelist.clear();
    m_router = Pistache::Rest::Router();
    for (auto& route : m_http_routes) {
        setup_route(route, true);
    }
    start();
}

void HttpServer::setup_route(Pistache::Http::Method method, std::string resource,
                             Pistache::Rest::Route::Handler handler, url_type const& type) {
    DEBUG_ASSERT(!m_server_running, "Initiated route setup after server started");
    if (m_server_running) {
        LOGWARN("Could not setup route {}, server is in running state.", resource)
        return;
    }

    m_router.addRoute(std::move(method), resource, std::move(handler));

    if (type == url_type::localhost) {
        m_localhost_list.emplace(std::move(resource));
    } else if (type == url_type::safe) {
        m_safelist.emplace(std::move(resource));
    }
}

void HttpServer::setup_route(http_route const& route, bool restart) {
    if (!restart) { m_http_routes.push_back(route); }
    setup_route(route.method, route.resource, route.handler, route.type);
}

void HttpServer::setup_routes(std::vector< http_route > const& routes) {
    for (auto& route : routes) {
        setup_route(route, false);
    }
}

void HttpServer::setup_ssl(std::string const& ssl_cert, std::string const& ssl_key) {
    if (m_secure_zone) {
        namespace fs = std::filesystem;
        auto wait_for_file = [](std::string const& path) {
            while (true) {
                if (fs::exists(path) && fs::file_size(fs::path{path}) > 0) { return; }
                LOGINFO("File {} not available, will try in 5 seconds", path);
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        };
        wait_for_file(ssl_cert);
        wait_for_file(ssl_key);
#ifdef PISTACHE_USE_SSL
        m_http_endpoint->useSSL(ssl_cert, ssl_key);
#else
        LOGWARN("SSL requested but Pistache was built without SSL support; ignoring certs");
#endif
    }
}

bool HttpServer::do_auth(Pistache::Http::Request& request, Pistache::Http::ResponseWriter& response) {
    if (is_safe_url(request.resource())) { return true; }
    if (is_localaddr_url(request.resource())) { return is_local_addr(request.address().host()); }
    if (m_token_verifier) { return auth_verify(request, response); }
    return true;
}

bool HttpServer::auth_verify(Pistache::Http::Request& request, Pistache::Http::ResponseWriter& response) const {
    auto opt_token = request.headers().tryGet< Pistache::Http::Header::Authorization >();
    if (!opt_token) {
        response.send(Pistache::Http::Code::Unauthorized, "missing auth token in request header");
        return false;
    }

    if (!opt_token->hasMethod< Pistache::Http::Header::Authorization::Method::Bearer >()) {
        response.send(Pistache::Http::Code::Unauthorized, "require bearer token in request header");
        return false;
    }

    auto const prefix_len = std::string{"Bearer "}.length();
    auto ret_state = m_token_verifier->verify(opt_token->value().substr(prefix_len));
    if (ret_state->code == sisl::VerifyCode::OK) { return true; }
    response.send(to_pistache_code(ret_state), ret_state->msg);
    return false;
}

void HttpServer::get_local_ips() {
    struct ifaddrs* interfaces = nullptr;
    auto error = getifaddrs(&interfaces);
    if (error != 0) { LOGWARN("getifaddrs returned non zero code: {}", error); }
    for (auto* addr = interfaces; addr != nullptr; addr = addr->ifa_next) {
        if (addr->ifa_addr->sa_family == AF_INET) {
            m_local_ips.emplace(inet_ntoa(((struct sockaddr_in*)addr->ifa_addr)->sin_addr));
        }
    }
    freeifaddrs(interfaces);
}

bool HttpServer::is_localaddr_url(std::string const& url) const { return m_localhost_list.count(url) > 0; }
bool HttpServer::is_safe_url(std::string const& url) const { return m_safelist.count(url) > 0; }
bool HttpServer::is_secure_zone() const { return m_secure_zone; }
bool HttpServer::is_local_addr(std::string const& addr) const { return m_local_ips.count(addr) > 0; }

#ifdef PROMETHEUS_METRICS_REPORTER
void HttpServer::register_metrics_endpoint() {
    setup_route(
        Pistache::Http::Method::Get, "/metrics",
        [](Pistache::Http::Request const&, Pistache::Http::ResponseWriter response) {
            response.send(Pistache::Http::Code::Ok,
                          sisl::MetricsFarm::getInstance().report(sisl::ReportFormat::kTextFormat));
        },
        url_type::safe);
}
#endif

} // namespace sisl
