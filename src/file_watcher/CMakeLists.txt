cmake_minimum_required (VERSION 3.11)

add_library(sisl_file_watcher OBJECT)
target_sources(sisl_file_watcher PRIVATE
    file_watcher.cpp
  )
target_link_libraries(sisl_file_watcher ${COMMON_DEPS})

add_executable(test_file_watcher)
target_sources(test_file_watcher PRIVATE
    file_watcher_test.cpp
  )
target_link_libraries(test_file_watcher sisl ${COMMON_DEPS} GTest::gtest GTest::gmock)
add_test(NAME test_file_watcher COMMAND test_file_watcher)
