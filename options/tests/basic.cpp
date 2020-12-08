//
// Copyright 2018, eBay Corporation
//

#include <iostream>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "options.h"

SDS_OPTION_GROUP(logging, (verbosity, "v", "verbosity", "Verbosity  level (0-5)", ::cxxopts::value<uint32_t>()->default_value("2"), "level"),
                          (synclog, "s", "synclog", "Synchronized logging", ::cxxopts::value<bool>(), ""))


SDS_OPTIONS_ENABLE(logging)

TEST(Options, Loaded) {
EXPECT_EQ(2u, SDS_OPTIONS["verbosity"].as<uint32_t>());
}

int main(int argc, char* argv[]) {
   ::testing::InitGoogleTest(&argc, argv);
   argc = 1;
   SDS_OPTIONS_LOAD(argc, argv, logging);
   if (SDS_OPTIONS.count("synclog")) {
      std::cout << "Sync log enabled!" << std::endl;
   }
   return RUN_ALL_TESTS();
}

