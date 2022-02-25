//
// Created by Kadayam, Hari on 12/14/18.
//
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <cpr/cpr.h>
#include <event2/http.h>

#include <gtest/gtest.h>

#include "http_server.hpp"

SISL_LOGGING_INIT(httpserver_lmod)
SISL_OPTIONS_ENABLE(logging)

namespace {
sisl::HttpServerConfig s_cfg;
std::unique_ptr< sisl::HttpServer > s_server;
std::mutex s_m;
std::condition_variable s_cv;
bool s_is_shutdown{false};
std::unique_ptr< std::thread > s_timer_thread;
} // namespace

static void sleep_and_return(sisl::HttpCallData cd, int64_t secs) {
    std::this_thread::sleep_for(std::chrono::seconds{secs});
    std::ostringstream ss{};
    ss << "Took a good nap for " << secs << " seconds. Thank you!\n";
    s_server->respond_OK(cd, EVHTP_RES_OK, ss.str());
}

static void delayed_return(sisl::HttpCallData cd) {
    const auto req{cd->request()};
    const auto t{::evhtp_kvs_find_kv(req->uri->query, "seconds")};
    if (!t) {
        s_server->respond_NOTOK(cd, EVHTP_RES_BADREQ, "Invalid seconds param!");
        return;
    }

    std::string sstr{t->val};
    if (sstr.empty() || !std::all_of(sstr.begin(), sstr.end(), ::isdigit)) {
        s_server->respond_NOTOK(cd, EVHTP_RES_BADREQ,
                                "Invalid seconds param! Either empty or contains non-digit characters\n");
        return;
    }

    const int64_t secs{std::stoll(sstr, nullptr, 10)};
    s_timer_thread = std::make_unique< std::thread >(sleep_and_return, cd, secs);
    return;
}

static void say_hello(sisl::HttpCallData cd) {
    std::cout << "Client is saying hello\n";
    s_server->respond_OK(cd, EVHTP_RES_OK, "Hello client from async_http server\n");
}

static void say_name(sisl::HttpCallData cd) {
    s_server->respond_OK(cd, EVHTP_RES_OK, "I am the sisl (sizzling) http server \n");
}

static void shutdown(sisl::HttpCallData cd) {
    std::cout << "Client is asking us to shutdown server\n";
    s_server->respond_OK(cd, EVHTP_RES_OK, "Ok will do\n");

    {
        std::lock_guard< std::mutex > lk{s_m};
        s_is_shutdown = true;
    }
    s_cv.notify_one();
}

class HTTPServerTest : public ::testing::Test {
public:
    HTTPServerTest() = default;
    HTTPServerTest(const HTTPServerTest&) = delete;
    HTTPServerTest& operator=(const HTTPServerTest&) = delete;
    HTTPServerTest(HTTPServerTest&&) noexcept = delete;
    HTTPServerTest& operator=(HTTPServerTest&&) noexcept = delete;
    virtual ~HTTPServerTest() override = default;

    virtual void SetUp() override {
        s_server = std::make_unique< sisl::HttpServer >(
            s_cfg,
            std::vector< sisl::_handler_info >{handler_info("/api/v1/sayHello", say_hello, nullptr),
                                               handler_info("/api/v1/shutdown", shutdown, nullptr),
                                               handler_info("/api/v1/sleepFor", delayed_return, nullptr)});
        s_is_shutdown = false;
        s_server->start();
    }

    virtual void TearDown() override {
        s_server->stop();

        if (s_timer_thread && s_timer_thread->joinable()) { s_timer_thread->join(); }
        s_timer_thread.reset();
        s_server.reset();
    }

protected:
    void wait_for_shutdown() {
        std::unique_lock< std::mutex > lk{s_m};
        s_cv.wait(lk, [] { return (s_is_shutdown); });
    }
};

TEST_F(HTTPServerTest, BasicTest) {
    s_server->register_handler_info(handler_info("/api/v1/yourNamePlease", say_name, nullptr));

    const cpr::Url url{"http://127.0.0.1:5051/api/v1/shutdown"};
    const auto resp{cpr::Post(url)};

    ASSERT_EQ(resp.status_code, cpr::status::HTTP_OK);

    wait_for_shutdown();

#ifdef _PRERELEASE
    std::cout << "ObjectLife Counter:\n";
    sisl::ObjCounterRegistry::foreach ([](const std::string& name, int64_t created, int64_t alive) {
        std::cout << name << ": " << alive << "/" << created << "\n";
    });
#endif
}

TEST_F(HTTPServerTest, ParallelTestWithWait) {
    s_server->register_handler_info(handler_info("/api/v1/yourNamePlease", say_name, nullptr));

    std::atomic< bool > failed{false};
    const auto thread_func{[&failed](const size_t iterations) {
        const cpr::Url url{"http://127.0.0.1:5051/api/v1/yourNamePlease"};
        for (size_t iteration{0}; (iteration < iterations) && !failed; ++iteration) {
            const auto resp{cpr::Post(url)};
            if (resp.status_code != cpr::status::HTTP_OK) failed = true;
        }
    }};

    constexpr size_t num_iterations{100};
    const size_t num_threads{std::max< size_t >(std::thread::hardware_concurrency(), 2)};
    std::vector< std::thread > workers;
    for (size_t thread_num{0}; thread_num < num_threads; ++thread_num) {
        workers.emplace_back(thread_func, num_iterations);
    }

    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
    }

    ASSERT_FALSE(failed);
}

TEST_F(HTTPServerTest, ParallelTestWithoutWait) {
    s_server->register_handler_info(handler_info("/api/v1/yourNamePlease", say_name, nullptr));

    const auto thread_func{[](const size_t iterations) {
        const cpr::Url url{"http://127.0.0.1:5051/api/v1/yourNamePlease"};
        for (size_t iteration{0}; iteration < iterations; ++iteration) {
            [[maybe_unused]] auto response{cpr::PostAsync(url)};
        }
    }};

    constexpr size_t num_iterations{100};
    const size_t num_threads{std::max< size_t >(std::thread::hardware_concurrency(), 2)};
    std::vector< std::thread > workers;
    for (size_t thread_num{0}; thread_num < num_threads; ++thread_num) {
        workers.emplace_back(thread_func, num_iterations);
    }

    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
    }

    // exit while server processing
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging)

    s_cfg.is_tls_enabled = false;
    s_cfg.bind_address = "127.0.0.1";
    s_cfg.server_port = 5051;
    s_cfg.read_write_timeout_secs = 10;

    return RUN_ALL_TESTS();
}
