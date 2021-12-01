/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on  * an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "cache.hpp"
#include "tree.hpp"
#include <gtest/gtest.h>

#define ITERATIONS 4

RCU_REGISTER_INIT

using namespace sisl;

TEST(counterTest, wrapperTest) {
    Tree tree1("tree1"), tree2("tree2");
    tree1.update();
    tree2.update();

    Cache cache1("cache1"), cache2("cache2");
    cache1.update();
    cache2.update();

    auto output = MetricsFarm::getInstance().get_result_in_json_string();
    std::cout << "Output of gather = " << output << "\n";
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
