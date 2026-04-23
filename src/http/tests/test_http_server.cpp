#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <sisl/auth_manager/token_verifier.hpp>
#include <sisl/http/http_server.hpp>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

SISL_LOGGING_INIT(http)
SISL_OPTIONS_ENABLE(logging)

namespace {

static std::atomic< uint16_t > s_next_port{18080};
uint16_t next_port() { return s_next_port.fetch_add(1); }

// Sends a raw HTTP/1.1 GET and returns the full response.
std::string http_get(uint16_t port, std::string const& path, std::string const& auth_header = "") {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return {};

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(sock, reinterpret_cast< struct sockaddr* >(&addr), sizeof(addr)) != 0) {
        ::close(sock);
        return {};
    }

    struct timeval tv{2, 0};
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::string req = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n";
    if (!auth_header.empty()) { req += "Authorization: " + auth_header + "\r\n"; }
    req += "\r\n";
    ::send(sock, req.c_str(), req.size(), 0);

    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        response += buf;
    }
    ::close(sock);
    return response;
}

int parse_status(std::string const& response) {
    auto pos = response.find(' ');
    if (pos == std::string::npos) return 0;
    try {
        return std::stoi(response.substr(pos + 1, 3));
    } catch (...) { return 0; }
}

bool wait_for_server(uint16_t port, int attempts = 40) {
    for (int i = 0; i < attempts; ++i) {
        int sock = ::socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        bool ok = ::connect(sock, reinterpret_cast< struct sockaddr* >(&addr), sizeof(addr)) == 0;
        ::close(sock);
        if (ok) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

// Accepts "valid-token", rejects everything else.
class MockVerifier : public sisl::TokenVerifier {
public:
    sisl::token_state_ptr verify(std::string const& token) const override {
        if (token == "valid-token") { return std::make_shared< sisl::TokenVerifyState >(sisl::VerifyCode::OK, ""); }
        return std::make_shared< sisl::TokenVerifyState >(sisl::VerifyCode::UNAUTH, "invalid token");
    }
};

Pistache::Rest::Route::Result ok_handler(Pistache::Rest::Request const&, Pistache::Http::ResponseWriter resp) {
    resp.send(Pistache::Http::Code::Ok, "ok");
    return Pistache::Rest::Route::Result::Ok;
}

} // namespace

// ---- URL classification (no server start needed) ----

TEST(HttpServer, UrlClassification) {
    sisl::HttpServer server{next_port()};
    server.setup_route(Pistache::Http::Method::Get, "/safe", ok_handler, sisl::url_type::safe);
    server.setup_route(Pistache::Http::Method::Get, "/local", ok_handler, sisl::url_type::localhost);
    server.setup_route(Pistache::Http::Method::Get, "/regular", ok_handler, sisl::url_type::regular);

    EXPECT_TRUE(server.is_safe_url("/safe"));
    EXPECT_FALSE(server.is_safe_url("/local"));
    EXPECT_FALSE(server.is_safe_url("/regular"));

    EXPECT_TRUE(server.is_localaddr_url("/local"));
    EXPECT_FALSE(server.is_localaddr_url("/safe"));
    EXPECT_FALSE(server.is_localaddr_url("/regular"));
}

TEST(HttpServer, SecureZoneFlag) {
    sisl::HttpServer plain{next_port()};
    EXPECT_FALSE(plain.is_secure_zone());

    // SSL constructor with empty strings → not secure (one-but-not-both check logs an error)
    sisl::HttpServer also_plain{"", "", next_port()};
    EXPECT_FALSE(also_plain.is_secure_zone());
}

// ---- Live HTTP tests ----

TEST(HttpServer, SafeRouteReturns200) {
    uint16_t port = next_port();
    sisl::HttpServer server{port};
    server.setup_route(Pistache::Http::Method::Get, "/health", ok_handler, sisl::url_type::safe);
    server.start();
    ASSERT_TRUE(wait_for_server(port));

    EXPECT_EQ(parse_status(http_get(port, "/health")), 200);

    server.stop();
}

TEST(HttpServer, RegularRouteNoVerifierReturns200) {
    uint16_t port = next_port();
    sisl::HttpServer server{port};
    server.setup_route(Pistache::Http::Method::Get, "/data", ok_handler, sisl::url_type::regular);
    server.start();
    ASSERT_TRUE(wait_for_server(port));

    EXPECT_EQ(parse_status(http_get(port, "/data")), 200);

    server.stop();
}

TEST(HttpServer, LocalhostRouteFromLoopbackReturns200) {
    uint16_t port = next_port();
    sisl::HttpServer server{port};
    server.setup_route(Pistache::Http::Method::Get, "/local", ok_handler, sisl::url_type::localhost);
    server.start();
    ASSERT_TRUE(wait_for_server(port));

    // Connecting from 127.0.0.1, which is in m_local_ips
    EXPECT_EQ(parse_status(http_get(port, "/local")), 200);

    server.stop();
}

TEST(HttpServer, RegularRouteNoTokenReturns401) {
    uint16_t port = next_port();
    MockVerifier verifier;
    sisl::HttpServer server{port, 1, 4000000, &verifier};
    server.setup_route(Pistache::Http::Method::Get, "/secure", ok_handler, sisl::url_type::regular);
    server.start();
    ASSERT_TRUE(wait_for_server(port));

    EXPECT_EQ(parse_status(http_get(port, "/secure")), 401);

    server.stop();
}

TEST(HttpServer, RegularRouteValidTokenReturns200) {
    uint16_t port = next_port();
    MockVerifier verifier;
    sisl::HttpServer server{port, 1, 4000000, &verifier};
    server.setup_route(Pistache::Http::Method::Get, "/secure", ok_handler, sisl::url_type::regular);
    server.start();
    ASSERT_TRUE(wait_for_server(port));

    EXPECT_EQ(parse_status(http_get(port, "/secure", "Bearer valid-token")), 200);

    server.stop();
}

TEST(HttpServer, RegularRouteInvalidTokenReturns401) {
    uint16_t port = next_port();
    MockVerifier verifier;
    sisl::HttpServer server{port, 1, 4000000, &verifier};
    server.setup_route(Pistache::Http::Method::Get, "/secure", ok_handler, sisl::url_type::regular);
    server.start();
    ASSERT_TRUE(wait_for_server(port));

    EXPECT_EQ(parse_status(http_get(port, "/secure", "Bearer wrong-token")), 401);

    server.stop();
}

int main(int argc, char* argv[]) {
    SISL_OPTIONS_LOAD(argc, argv, logging);
    sisl::logging::SetLogger("test_http_server");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
