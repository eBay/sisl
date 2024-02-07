cmake_minimum_required (VERSION 3.11)

add_library(sisl_file_watcher)
target_sources(sisl_file_watcher PRIVATE
  file_watcher.cpp
  )
target_link_libraries(sisl_file_watcher PUBLIC
  sisl_logging)

if (DEFINED ENABLE_TESTING)
  if (${ENABLE_TESTING})
    add_executable(test_file_watcher)
    target_sources(test_file_watcher PRIVATE
      file_watcher_test.cpp
      )
    target_link_libraries(test_file_watcher sisl_file_watcher GTest::gtest GTest::gmock)
    add_test(NAME FileWatcher COMMAND test_file_watcher)
  endif()
endif()
