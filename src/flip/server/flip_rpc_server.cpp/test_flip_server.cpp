//
// Created by Kadayam, Hari on 28/03/18.
//

#include "flip.hpp"

#include <sds_options/options.h>

SDS_LOGGING_INIT(flip)

SDS_OPTIONS_ENABLE(logging)

int main(int argc, char *argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging)
    sds_logging::SetLogger(std::string(argv[0]));
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    flip::Flip f;
    f.start_rpc_server();

    sleep(1000);
    return 0;
}
