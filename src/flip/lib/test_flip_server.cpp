//
// Created by Kadayam, Hari on 28/03/18.
//

#include "flip.hpp"

#include "options/options.h"

SISL_LOGGING_INIT(flip)

SISL_OPTIONS_ENABLE(logging)

int main(int argc, char* argv[]) {
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger(std::string(argv[0]));
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    flip::Flip f;
    f.start_rpc_server();

    sleep(1000);
    return 0;
}
