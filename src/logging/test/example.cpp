/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Brian Szmyd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <thread>

#include <sisl/options/options.h>

#include <sisl/logging/logging.h>

SISL_LOGGING_DECL(my_module)
SISL_LOGGING_DEF(my_module)
SISL_LOGGING_INIT(my_module)

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
SISL_OPTION_GROUP(test_logging,
    (signal, "si", "signal option", "signal option", ::cxxopts::value<uint32_t>(), "1-6"))
// clang-format on

#define ENABLED_OPTIONS test_logging, logging

SISL_OPTIONS_ENABLE(ENABLED_OPTIONS)

int main(int argc, char** argv) {
    SISL_OPTIONS_LOAD(argc, argv, ENABLED_OPTIONS)
    sisl::logging::SetLogger(std::string{argv[0]});
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    SISL_LOG_LEVEL(my_module, spdlog::level::level_enum::trace);
    sisl::logging::install_crash_handler();

    std::thread t{func};
    std::this_thread::sleep_for(std::chrono::seconds{1});
    std::cout << "spdlog level base = " << module_level_base << "\n";
    LOGTRACE("Trace");
    LOGDEBUG("Debug");
    LOGINFO("Info");
    LOGWARN("Warning");
    LOGERROR("Error");
    LOGCRITICAL("Critical");

    SISL_LOG_LEVEL(my_module, spdlog::level::level_enum::info);
    LOGINFOMOD(my_module, "Enabled Module Logger");
    LOGTRACEMOD(my_module, "Trace Module");

    // RELEASE_ASSERT_EQ(0, 1, "test");

    // sisl::logging::log_stack_trace(true);
#if 0
    sisl::logging::log_stack_trace();
#else
    /*
    // NOTE: Some reason signal not being recognized as option
    switch (SISL_OPTIONS["signal"].as< uint32_t >()) {
    case 1:
        std::raise(SIGABRT);
    case 2:
        std::raise(SIGFPE);
    case 3:
        std::raise(SIGSEGV);
    case 4:
        std::raise(SIGILL);
    case 5:
        std::raise(SIGTERM);
    case 6:
        std::raise(SIGINT);
    default: break;
    }
    */

    std::raise(SIGABRT);
    // std::raise(SIGFPE);
    // std::raise(SIGSEGV);
    // std::raise(SIGILL);
    // std::raise(SIGTERM);
    // std::raise(SIGINT);

    /*
    int* ptr{nullptr};
    [[maybe_unused]] int i{*ptr};
    std::cout << i << std::endl;
    */

#endif

    if (t.joinable()) t.join();
    return 0;
}
