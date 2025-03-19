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

add_executable(logging_example_v2)
target_sources(logging_example_v2 PRIVATE
  test/example_v2.cpp
  )
target_link_libraries(logging_example_v2 ${LOGGING_DEPS} sisl_logging)