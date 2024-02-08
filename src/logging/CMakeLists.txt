cmake_minimum_required (VERSION 3.11)

list(APPEND LOGGING_DEPS
  sisl_options
  spdlog::spdlog
  nlohmann_json::nlohmann_json
  )
if(${breakpad_FOUND})
  list(APPEND LOGGING_DEPS breakpad::breakpad)
endif()

add_library(sisl_logging)
target_sources(sisl_logging PRIVATE
  logging.cpp
  stacktrace.cpp
  )
target_link_libraries(sisl_logging PUBLIC ${LOGGING_DEPS})

if (DEFINED ENABLE_TESTING)
  if (${ENABLE_TESTING})
    add_executable(logging_example)
    target_sources(logging_example PRIVATE
      test/example.cpp
      )
    target_link_libraries(logging_example sisl_logging)
  endif()
endif()
