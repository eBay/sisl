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

static void sleep_and_return(evhtp_request_t* req, int64_t secs) {
    sleep(secs);
    std::stringstream ss; ss << "Took a good nap for " << secs << " seconds. Thank you!\n";
    server->respond_OK(req, EVHTP_RES_OK, ss.str());
}

static void delayed_return(evhtp_request_t* req, void* task) {
    auto t = evhtp_kvs_find_kv(req->uri->query, "seconds");
    if (!t) {
        server->respond_NOTOK(req, EVHTP_RES_BADREQ, "Invalid seconds param!");
        return;
    }

    std::string sstr = t->val;
    if (sstr.empty() || !std::all_of(sstr.begin(), sstr.end(), ::isdigit)) {
        server->respond_NOTOK(req, EVHTP_RES_BADREQ, "Invalid seconds param! Either empty or contains non-digit characters\n");
        return;
    }

    int64_t secs = strtoll(sstr.c_str(), NULL, 10);
    m_timer_thread = new std::thread(sleep_and_return, req, secs);
    return;
}

static void say_hello(evhtp_request_t* req, void* task) {
    std::cout << "Client is saying hello\n";
    server->respond_OK(req, EVHTP_RES_OK, "Hello client from async_http server\n");
}

static void shutdown(evhtp_request_t* req, void* task) {
    std::cout << "Client is asking us to shutdown server\n";
    server->respond_OK(req, EVHTP_RES_OK, "Ok will do\n");

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
            {"/api/v1/sayHello", say_hello},
            {"/api/v1/shutdown", shutdown},
            {"/api/v1/sleepFor", delayed_return}
    });
    server->start();

    {
        std::unique_lock <std::mutex> lk(m);
        cv.wait(lk, [] { return (is_shutdown); });
    }

    server->stop();
    if (m_timer_thread) { m_timer_thread->join(); }
    delete(server);
    return 0;
}