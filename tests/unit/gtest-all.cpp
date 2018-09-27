/*
 * gtest-all.cpp
 *
 *  Created on: Sep 18, 2018
 */


#include <gtest/gtest.h>
#include <sds_logging/logging.h>

using log_level = spdlog::level::level_enum;

int main(int argc, char** argv) {

	::testing::InitGoogleTest(&argc, argv);
	int ret = RUN_ALL_TESTS();
	return ret;
}

