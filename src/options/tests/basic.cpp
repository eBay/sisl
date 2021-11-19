//
// Copyright 2018, eBay Corporation
//

#include <iostream>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "options.h"

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
