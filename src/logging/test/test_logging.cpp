/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
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
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

// DEF is needed so dlsym() can find module_level_test_logging for SetModuleLogLevel/GetModuleLogLevel
SISL_LOGGING_DEF(test_logging)
SISL_LOGGING_INIT(test_logging)
SISL_OPTIONS_ENABLE(logging)

// Covers GetCriticalLogger() (lines 120-124 in logging.cpp)
TEST(LoggingTest, CriticalLoggerIsNonNull) {
    auto& logger = sisl::logging::GetCriticalLogger();
    EXPECT_NE(logger, nullptr);
    EXPECT_EQ(logger->level(), spdlog::level::err);
}

// Covers SetLogPattern() (lines 323-329 in logging.cpp)
TEST(LoggingTest, SetLogPattern) {
    EXPECT_NO_THROW(sisl::logging::SetLogPattern("%v"));
    EXPECT_NO_THROW(sisl::logging::SetLogPattern("%v", sisl::logging::GetLogger()));
    // Reset to a standard pattern
    sisl::logging::SetLogPattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");
}

// Covers CreateCustomLogger() (lines 331-354 in logging.cpp)
TEST(LoggingTest, CreateCustomLogger) {
    auto logger = sisl::logging::CreateCustomLogger("test_custom", "_custom", true /* tee_to_stdout */);
    EXPECT_NE(logger, nullptr);
    EXPECT_NO_THROW(logger->info("custom logger works"));
}

// Covers SetModuleLogLevel() and GetModuleLogLevel() (lines 356-364 in logging.cpp)
TEST(LoggingTest, ModuleLogLevel) {
    sisl::logging::SetModuleLogLevel("test_logging", spdlog::level::debug);
    EXPECT_EQ(sisl::logging::GetModuleLogLevel("test_logging"), spdlog::level::debug);

    sisl::logging::SetModuleLogLevel("test_logging", spdlog::level::info);
    EXPECT_EQ(sisl::logging::GetModuleLogLevel("test_logging"), spdlog::level::info);
}

// Covers GetAllModuleLogLevel() (lines 366-373 in logging.cpp)
TEST(LoggingTest, GetAllModuleLogLevel) {
    auto j = sisl::logging::GetAllModuleLogLevel();
    EXPECT_TRUE(j.contains("test_logging"));
}

// Covers SetAllModuleLogLevel() (lines 375-380 in logging.cpp)
TEST(LoggingTest, SetAllModuleLogLevel) {
    EXPECT_NO_THROW(sisl::logging::SetAllModuleLogLevel(spdlog::level::warn));
    EXPECT_EQ(sisl::logging::GetModuleLogLevel("test_logging"), spdlog::level::warn);
    sisl::logging::SetAllModuleLogLevel(spdlog::level::info);
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger("test_logging");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");
    return RUN_ALL_TESTS();
}
