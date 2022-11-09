cmake_minimum_required (VERSION 3.10)

add_flags("-Wno-unused-parameter")

include_directories(BEFORE .)

set(FILE_WATCHER_SOURCE_FILES 
    file_watcher.cpp
   )
add_library(sisl_file_watcher OBJECT ${FILE_WATCHER_SOURCE_FILES})
target_link_libraries(sisl_file_watcher ${COMMON_DEPS})

set(TEST_FILE_WATCHER_SOURCES
    file_watcher_test.cpp
   )
add_executable(test_file_watcher ${TEST_FILE_WATCHER_SOURCES})
target_link_libraries(test_file_watcher sisl ${COMMON_DEPS} GTest::gtest GTest::gmock)
add_test(NAME test_file_watcher COMMAND test_file_watcher)
