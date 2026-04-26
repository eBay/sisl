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

static int to_http_status(sisl::token_state_ptr const& status) {
    switch (status->code) {
    case sisl::VerifyCode::OK:
        return 200;
    case sisl::VerifyCode::UNAUTH:
        return 401;
    case sisl::VerifyCode::FORBIDDEN:
        return 403;
    default:
        break;
    }
    return 412;
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

HttpServer::~HttpServer() {
    if (m_server_running) { stop(); }
}

void HttpServer::init() {
    if (m_secure_zone) {
        namespace fs = std::filesystem;
        auto wait_for_file = [](std::string const& path) {
            while (true) {
                if (fs::exists(path) && fs::file_size(fs::path{path}) > 0) { return; }
                LOGINFO("File {} not available, will try in 5 seconds", path);
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        };
        wait_for_file(m_ssl_cert);
        wait_for_file(m_ssl_key);
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        m_server = std::make_unique< httplib::SSLServer >(m_ssl_cert.c_str(), m_ssl_key.c_str());
#else
        LOGWARN("SSL requested but cpp-httplib was built without OpenSSL support; ignoring certs");
        m_server = std::make_unique< httplib::Server >();
#endif
    } else {
        m_server = std::make_unique< httplib::Server >();
    }
    m_server->new_task_queue = [n = m_num_threads] { return new httplib::ThreadPool(n); };
    m_server->set_payload_max_length(m_max_request_size);
}

void HttpServer::start() {
    m_server->set_pre_routing_handler([this](httplib::Request const& req, httplib::Response& res) {
        return do_auth(req, res) ? httplib::Server::HandlerResponse::Unhandled
                                 : httplib::Server::HandlerResponse::Handled;
    });
    if (!m_server->bind_to_port("0.0.0.0", m_port)) {
        LOGERROR("Failed to bind HTTP server to port {}", m_port);
        return;
    }
    m_server_thread = std::thread([this] { m_server->listen_after_bind(); });
    m_server_running = true;
}

void HttpServer::stop() {
    m_server->stop();
    if (m_server_thread.joinable()) { m_server_thread.join(); }
    m_server_running = false;
}

void HttpServer::restart(std::string const& ssl_cert, std::string const& ssl_key) {
    std::unique_lock< std::mutex > lock(m_mutex);
    stop();
    m_ssl_cert = ssl_cert;
    m_ssl_key = ssl_key;
    m_secure_zone = !ssl_cert.empty() && !ssl_key.empty();
    m_localhost_list.clear();
    m_safelist.clear();
    init();
    for (auto& route : m_http_routes) {
        setup_route(route, true);
    }
    start();
}

void HttpServer::setup_route(http_method method, std::string resource, http_handler handler, url_type const& type) {
    DEBUG_ASSERT(!m_server_running, "Initiated route setup after server started");
    if (m_server_running) {
        LOGWARN("Could not setup route {}, server is in running state.", resource)
        return;
    }

    switch (method) {
    case http_method::Get:
        m_server->Get(resource, handler);
        break;
    case http_method::Post:
        m_server->Post(resource, handler);
        break;
    case http_method::Put:
        m_server->Put(resource, handler);
        break;
    case http_method::Delete:
        m_server->Delete(resource, handler);
        break;
    case http_method::Patch:
        m_server->Patch(resource, handler);
        break;
    case http_method::Options:
        m_server->Options(resource, handler);
        break;
    }

    if (type == url_type::localhost) {
        m_localhost_list.emplace(resource);
    } else if (type == url_type::safe) {
        m_safelist.emplace(resource);
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

bool HttpServer::do_auth(httplib::Request const& req, httplib::Response& res) {
    if (is_safe_url(req.path)) { return true; }
    if (is_localaddr_url(req.path)) {
        if (is_local_addr(req.remote_addr)) { return true; }
        res.status = 403;
        res.set_content("access restricted to localhost", "text/plain");
        return false;
    }
    if (m_token_verifier) { return auth_verify(req, res); }
    return true;
}

bool HttpServer::auth_verify(httplib::Request const& req, httplib::Response& res) const {
    static constexpr std::string_view bearer_prefix{"Bearer "};
    auto const auth = req.get_header_value("Authorization");
    if (auth.empty()) {
        res.status = 401;
        res.set_content("missing auth token in request header", "text/plain");
        return false;
    }
    if (!auth.starts_with(bearer_prefix)) {
        res.status = 401;
        res.set_content("require bearer token in request header", "text/plain");
        return false;
    }
    auto ret_state = m_token_verifier->verify(auth.substr(bearer_prefix.size()));
    if (ret_state->code == sisl::VerifyCode::OK) { return true; }
    res.status = to_http_status(ret_state);
    res.set_content(ret_state->msg, "text/plain");
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
        http_method::Get, "/metrics",
        [](httplib::Request const&, httplib::Response& res) {
            res.set_content(sisl::MetricsFarm::getInstance().report(sisl::ReportFormat::kTextFormat), "text/plain");
        },
        url_type::safe);
}
#endif

} // namespace sisl
