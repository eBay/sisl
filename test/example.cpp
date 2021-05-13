#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <thread>

#include <sds_options/options.h>

#include "logging.h"

SDS_LOGGING_INIT(my_module)

void func() {
    LOGINFO("Thread func started");
    size_t i{0};
    while (i < 3) {
        LOGINFO("Thread func {}th iteration", i + 1);
        std::this_thread::sleep_for(std::chrono::seconds{3});
        ++i;
    }
}

// clang-format off
SDS_OPTION_GROUP(test_logging,
    (signal, "si", "signal option", "signal option", ::cxxopts::value<uint32_t>(), "1-6"))
// clang-format on

#define ENABLED_OPTIONS test_logging, logging

SDS_OPTIONS_ENABLE(ENABLED_OPTIONS)

int main(int argc, char** argv) {
    SDS_OPTIONS_LOAD(argc, argv, ENABLED_OPTIONS)
    sds_logging::SetLogger(std::string{argv[0]});
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    SDS_LOG_LEVEL(my_module, spdlog::level::level_enum::trace);
    sds_logging::install_crash_handler();

    std::thread t{func};
    std::this_thread::sleep_for(std::chrono::seconds{1});
    std::cout << "spdlog level base = " << module_level_base << "\n";
    LOGTRACE("Trace");
    LOGDEBUG("Debug");
    LOGINFO("Info");
    LOGWARN("Warning");
    LOGERROR("Error");
    LOGCRITICAL("Critical");

    SDS_LOG_LEVEL(my_module, spdlog::level::level_enum::info);
    LOGINFOMOD(my_module, "Enabled Module Logger");
    LOGTRACEMOD(my_module, "Trace Module");

#if 0
    sds_logging::log_stack_trace();
#else
    std::raise(SIGABRT);
    // std::raise(SIGFPE);
    // std::raise(SIGSEGV);
    // std::raise(SIGILL);
    // std::raise(SIGTERM);
    // std::raise(SIGINT);
#endif

    if (t.joinable()) t.join();
    return 0;
}
