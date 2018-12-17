//
// Created by Kadayam, Hari on 12/14/18.
//
#include "http_server.hpp"

SDS_LOGGING_INIT(httpserver_lmod);

SDS_OPTIONS_ENABLE(logging)

int main(int argc, char *argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging)

    sisl::HttpServerConfig cfg;
    cfg.is_tls_enabled = false;
    cfg.bind_address = "0.0.0.0";
    cfg.server_port = 5051;
    cfg.read_write_timeout_secs = 10;

    sisl::HttpServer server(cfg);
    server.start();

    server.stop();
    return 0;
}