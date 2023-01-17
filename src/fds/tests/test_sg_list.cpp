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
#include "sisl/fds/buffer.hpp"

SISL_LOGGING_INIT(test_sg_list)
SISL_OPTIONS_ENABLE(logging, test_sg_list)
SISL_OPTION_GROUP(test_sg_list,
                  (num_threads, "", "num_threads", "number of threads",
                   ::cxxopts::value< uint32_t >()->default_value("8"), "number"))

struct SgListTest : public testing::Test {};

// a test case that make sure iterator works as expected;
TEST_F(SgListTest, TestIterator) {
    // TO Be Implemented;
}

int main(int argc, char* argv[]) {
    int parsed_argc{argc};
    ::testing::InitGoogleTest(&parsed_argc, argv);
    SISL_OPTIONS_LOAD(parsed_argc, argv, logging, test_sg_list);
    sisl::logging::SetLogger("test_sg_list");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    const auto ret{RUN_ALL_TESTS()};
    return ret;
}
