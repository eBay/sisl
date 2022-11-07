/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Brian Szymd
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
#include <iostream>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sisl/options/options.h>

SISL_OPTION_GROUP(logging,
                  (verbosity, "v", "verbosity", "Verbosity  level (0-5)",
                   ::cxxopts::value< uint32_t >()->default_value("2"), "level"),
                  (synclog, "s", "synclog", "Synchronized logging", ::cxxopts::value< bool >(), ""))

SISL_OPTIONS_ENABLE(logging)

TEST(Options, Loaded) { EXPECT_EQ(2u, SISL_OPTIONS["verbosity"].as< uint32_t >()); }

TEST(Options, AllTrue) {
    const bool result1{sisl::options::all_true< true, true, true >::value};
    EXPECT_TRUE(result1);
    const bool result2{sisl::options::all_true< true, true, false >::value};
    const bool result3{sisl::options::all_true< true, false, true >::value};
    const bool result4{sisl::options::all_true< true, false, true >::value};
    EXPECT_FALSE(result2);
    EXPECT_FALSE(result3);
    EXPECT_FALSE(result4);
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    argc = 1;
    SISL_OPTIONS_LOAD(argc, argv, logging);
    if (SISL_OPTIONS.count("synclog")) { std::cout << "Sync log enabled!" << std::endl; }
    return RUN_ALL_TESTS();
}
