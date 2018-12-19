//
// Created by Kadayam, Hari on 12/14/18.
//
#include "http_server.hpp"
#include <memory>
#include <thread>
#include <sstream>
#include <fstream>

SDS_LOGGING_INIT(httpserver_lmod);

SDS_OPTIONS_ENABLE(logging)

sisl::HttpServer *server;
std::mutex m;
std::condition_variable cv;
bool is_shutdown = false;
std::thread *m_timer_thread = nullptr;

static void sleep_and_return(sisl::HttpCallData cd, int64_t secs) {
    sleep(secs);
    std::stringstream ss; ss << "Took a good nap for " << secs << " seconds. Thank you!\n";
    server->respond_OK(cd, EVHTP_RES_OK, ss.str());
}

static void delayed_return(sisl::HttpCallData cd) {
    auto req = cd->request();
    auto t = evhtp_kvs_find_kv(req->uri->query, "seconds");
    if (!t) {
        server->respond_NOTOK(cd, EVHTP_RES_BADREQ, "Invalid seconds param!");
        return;
    }

    std::string sstr = t->val;
    if (sstr.empty() || !std::all_of(sstr.begin(), sstr.end(), ::isdigit)) {
        server->respond_NOTOK(cd, EVHTP_RES_BADREQ, "Invalid seconds param! Either empty or contains non-digit characters\n");
        return;
    }

    int64_t secs = strtoll(sstr.c_str(), NULL, 10);
    m_timer_thread = new std::thread(sleep_and_return, cd, secs);
    return;
}

static void say_hello(sisl::HttpCallData cd) {
    std::cout << "Client is saying hello\n";
    server->respond_OK(cd, EVHTP_RES_OK, "Hello client from async_http server\n");
}

static void say_name(sisl::HttpCallData cd)  {
    server->respond_OK(cd, EVHTP_RES_OK, "I am the sisl (sizzling) http server \n");
}

static void shutdown(sisl::HttpCallData cd)  {
    std::cout << "Client is asking us to shutdown server\n";
    server->respond_OK(cd, EVHTP_RES_OK, "Ok will do\n");

    {
        std::lock_guard <std::mutex> lk(m);
        is_shutdown = true;
    }
    cv.notify_one();
}

int main(int argc, char *argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging)

    sisl::HttpServerConfig cfg;
    cfg.is_tls_enabled = false;
    cfg.bind_address = "0.0.0.0";
    cfg.server_port = 5051;
    cfg.read_write_timeout_secs = 10;

    server = new sisl::HttpServer(cfg, {
            handler_info("/api/v1/sayHello", say_hello, nullptr),
            handler_info("/api/v1/shutdown", shutdown, nullptr),
            handler_info("/api/v1/sleepFor", delayed_return, nullptr)
    });

    server->start();
    server->register_handler_info(handler_info("/api/v1/yourNamePlease", say_name, nullptr));

    {
        std::unique_lock <std::mutex> lk(m);
        cv.wait(lk, [] { return (is_shutdown); });
    }

#ifdef _PRERELEASE
    std::cout << "ObjectLife Counter:\n";
    sisl::ObjCounterRegistry::foreach([](const std::string& name, int64_t created, int64_t alive) {
        std::cout << name << ": " << alive << "/" << created << "\n";
    });
#endif

    server->stop();
    if (m_timer_thread) { m_timer_thread->join(); }
    delete(server);
    return 0;
}
