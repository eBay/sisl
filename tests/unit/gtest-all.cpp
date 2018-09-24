/*
 * gtest-all.cpp
 *
 *  Created on: Sep 18, 2018
 *      Author: lhuang8
 */


#include <gtest/gtest.h>


int main(int argc, char** argv) {
	::testing::InitGoogleTest(&argc, argv);
	int ret = RUN_ALL_TESTS();
	return ret;
}

