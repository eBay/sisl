cmake_minimum_required (VERSION 3.10)

if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-attributes") # needed for C++ 20 folly compilation
endif()

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(VERSION_SOURCE_FILES
    version.cpp
   )
add_library(sisl_version OBJECT ${VERSION_SOURCE_FILES})
target_link_libraries(sisl_version ${COMMON_DEPS} zmarok-semver::zmarok-semver)
target_include_directories(sisl_version PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(TEST_VERSION_SOURCE_FILES
    tests/test_version.cpp
   )
add_executable(test_version ${TEST_VERSION_SOURCE_FILES})
target_link_libraries(test_version sisl ${COMMON_DEPS} zmarok-semver::zmarok-semver GTest::gtest)
add_test(NAME VersionTest COMMAND test_version)
