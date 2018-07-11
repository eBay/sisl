//
// Copyright 2018, eBay Corporation
//

#include <iostream>
#include <string>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "options.h"

SDS_OPTION_GROUP(logging, (verbosity, "v", "verbosity", "Verbosity  level (0-5)", ::cxxopts::value<uint32_t>(), "level"),
                          (synclog, "s", "synclog", "Synchronized logging", ::cxxopts::value<bool>(), ""))

SDS_OPTIONS_ENABLE(logging)

int main(int argc, char* argv[]) {
   ::testing::InitGoogleTest(&argc, argv);
   SDS_OPTIONS_LOAD(argc, argv, logging);
   if (SDS_OPTIONS.count("synclog")) {
      std::cout << "Sync log enabled!" << std::endl;
   }
   return RUN_ALL_TESTS();
}

