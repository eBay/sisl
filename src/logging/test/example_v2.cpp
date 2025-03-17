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

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

SISL_OPTIONS_ENABLE(logging)

static void log_messages() {
    LOGINFOMOD(module1, "Module1 Info or lower enabled");
    LOGINFOMOD(module2, "Module2 Info or lower enabled");
    LOGINFOMOD(module3, "Module3 Info or lower enabled");
    LOGINFOMOD(module4, "Module4 Info or lower enabled");
    LOGINFOMOD(module5, "Module5 Info or lower enabled");
    LOGINFOMOD(module6, "Module6 Info or lower enabled");
    LOGTRACEMOD(module3, "Module3 Trace or lower enabled");
}

int main(int argc, char** argv) {
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger(std::string{argv[0]});
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    sisl::logging::install_crash_handler();

    REGISTER_LOG_MOD(module1);
    REGISTER_LOG_MOD(module2);
    REGISTER_LOG_MODS(module3, module4, module5);

    sisl::logging::SetModuleLogLevel("module1", spdlog::level::level_enum::info);
    sisl::logging::SetModuleLogLevel("module2", spdlog::level::level_enum::debug);
    sisl::logging::SetModuleLogLevel("module3", spdlog::level::level_enum::trace);
    sisl::logging::SetModuleLogLevel("module4", spdlog::level::level_enum::critical);
    sisl::logging::SetModuleLogLevel("module5", spdlog::level::level_enum::err);
    REGISTER_LOG_MODS(module6);
    sisl::logging::SetModuleLogLevel("module6", spdlog::level::level_enum::warn);

    auto j = sisl::logging::GetAllModuleLogLevel();
    LOGINFO("Modules and levels default: {}", j.dump(2));
    log_messages();

    sisl::logging::SetAllModuleLogLevel(spdlog::level::level_enum::debug);
    j = sisl::logging::GetAllModuleLogLevel();
    LOGINFO("Modules and levels after set all module log level: {}", j.dump(2));
    log_messages();

    return 0;
}
