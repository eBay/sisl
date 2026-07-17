#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>

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

void ok_handler(httplib::Request const&, httplib::Response& res) { res.set_content("ok", "text/plain"); }

} // namespace

// ---- Local IP discovery ----

namespace sisl {
// Defined in http_server.cpp; declared here rather than in the public header so <ifaddrs.h> stays out of
// sisl/http/http_server.hpp.
void collect_local_ipv4_addrs(struct ifaddrs* interfaces, std::unordered_set< std::string >& out);
} // namespace sisl

// getifaddrs(3) returns a null ifa_addr for interfaces with no assigned address -- a VPN or tunnel, say.
// Dereferencing it unconditionally segfaulted every HttpServer constructor on such a host, which no CI
// runner reproduces because lo/eth0 always carry an address. Hence the synthetic list.
TEST(HttpServer, LocalIpsSkipNullIfaddr) {
    struct sockaddr_in v4{};
    v4.sin_family = AF_INET;
    ASSERT_EQ(::inet_pton(AF_INET, "10.1.2.3", &v4.sin_addr), 1);

    struct sockaddr_in6 v6{};
    v6.sin6_family = AF_INET6;

    char tun_name[]{"tun0"}, eth_name[]{"eth0"}, v6_name[]{"eth1"};

    struct ifaddrs v6_if{};
    v6_if.ifa_name = v6_name;
    v6_if.ifa_addr = reinterpret_cast< struct sockaddr* >(&v6);
    v6_if.ifa_next = nullptr;

    struct ifaddrs v4_if{};
    v4_if.ifa_name = eth_name;
    v4_if.ifa_addr = reinterpret_cast< struct sockaddr* >(&v4);
    v4_if.ifa_next = &v6_if;

    struct ifaddrs tun_if{}; // address-less interface: the one that used to crash
    tun_if.ifa_name = tun_name;
    tun_if.ifa_addr = nullptr;
    tun_if.ifa_next = &v4_if;

    std::unordered_set< std::string > ips;
    sisl::collect_local_ipv4_addrs(&tun_if, ips);

    EXPECT_EQ(ips.size(), 1u);
    EXPECT_EQ(ips.count("10.1.2.3"), 1u);
}

TEST(HttpServer, LocalIpsEmptyList) {
    std::unordered_set< std::string > ips;
    sisl::collect_local_ipv4_addrs(nullptr, ips);
    EXPECT_TRUE(ips.empty());
}

// ---- URL classification (no server start needed) ----

TEST(HttpServer, UrlClassification) {
    sisl::HttpServer server{next_port()};
    server.setup_route(sisl::http_method::Get, "/safe", ok_handler, sisl::url_type::safe);
    server.setup_route(sisl::http_method::Get, "/local", ok_handler, sisl::url_type::localhost);
    server.setup_route(sisl::http_method::Get, "/regular", ok_handler, sisl::url_type::regular);

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
    server.setup_route(sisl::http_method::Get, "/health", ok_handler, sisl::url_type::safe);
    server.start();
    ASSERT_TRUE(wait_for_server(port));

    EXPECT_EQ(parse_status(http_get(port, "/health")), 200);

    server.stop();
}

TEST(HttpServer, RegularRouteNoVerifierReturns200) {
    uint16_t port = next_port();
    sisl::HttpServer server{port};
    server.setup_route(sisl::http_method::Get, "/data", ok_handler, sisl::url_type::regular);
    server.start();
    ASSERT_TRUE(wait_for_server(port));

    EXPECT_EQ(parse_status(http_get(port, "/data")), 200);

    server.stop();
}

TEST(HttpServer, LocalhostRouteFromLoopbackReturns200) {
    uint16_t port = next_port();
    sisl::HttpServer server{port};
    server.setup_route(sisl::http_method::Get, "/local", ok_handler, sisl::url_type::localhost);
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
    server.setup_route(sisl::http_method::Get, "/secure", ok_handler, sisl::url_type::regular);
    server.start();
    ASSERT_TRUE(wait_for_server(port));

    EXPECT_EQ(parse_status(http_get(port, "/secure")), 401);

    server.stop();
}

TEST(HttpServer, RegularRouteValidTokenReturns200) {
    uint16_t port = next_port();
    MockVerifier verifier;
    sisl::HttpServer server{port, 1, 4000000, &verifier};
    server.setup_route(sisl::http_method::Get, "/secure", ok_handler, sisl::url_type::regular);
    server.start();
    ASSERT_TRUE(wait_for_server(port));

    EXPECT_EQ(parse_status(http_get(port, "/secure", "Bearer valid-token")), 200);

    server.stop();
}

TEST(HttpServer, RegularRouteInvalidTokenReturns401) {
    uint16_t port = next_port();
    MockVerifier verifier;
    sisl::HttpServer server{port, 1, 4000000, &verifier};
    server.setup_route(sisl::http_method::Get, "/secure", ok_handler, sisl::url_type::regular);
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
