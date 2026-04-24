cmake_minimum_required (VERSION 3.11)

list(APPEND LOGGING_DEPS
  sisl_options
  spdlog::spdlog
  nlohmann_json::nlohmann_json
  )
if(${breakpad_FOUND})
  list(APPEND LOGGING_DEPS breakpad::breakpad)
endif()

list(APPEND LOGGING_SOURCE_FILES
  logging.cpp
  stacktrace.cpp
)
if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
  list(APPEND LOGGING_SOURCE_FILES backtrace.cpp)
endif()

add_library(sisl_logging)
target_sources(sisl_logging PRIVATE
  ${LOGGING_SOURCE_FILES}
)
target_link_libraries(sisl_logging PUBLIC ${LOGGING_DEPS} -rdynamic)
if(${breakpad_FOUND})
  target_compile_definitions(sisl_logging PRIVATE SISL_HAS_BREAKPAD)
endif()

add_executable(logging_example)
target_sources(logging_example PRIVATE
  test/example.cpp
  )
target_link_libraries(logging_example sisl_logging)

add_executable(test_logging)
target_sources(test_logging PRIVATE
  test/test_logging.cpp
  )
target_link_libraries(test_logging sisl_logging GTest::gtest)
add_test(NAME Logging COMMAND test_logging -c)
