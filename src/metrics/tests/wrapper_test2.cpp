//
// Created by Kadayam, Hari on 12/12/18.
//

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
