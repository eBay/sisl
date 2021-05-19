#include <version.hpp>
#include <sds_logging/logging.h>
#include <sds_options/options.h>
#include <iostream>

using namespace sisl;

SDS_OPTIONS_ENABLE(logging)
SDS_LOGGING_INIT(test_version)

int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging);
    sds_logging::SetLogger("test_version");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    std::stringstream ss;
    ss << sisl::get_version();
    LOGINFO("SISL ver. {}", ss.str());

    return 0;
}
