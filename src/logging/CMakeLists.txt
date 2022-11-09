cmake_minimum_required (VERSION 3.11)

add_library(sisl_logging OBJECT)
target_sources(sisl_logging PRIVATE
    backtrace.cpp
    logging.cpp
    stacktrace.cpp
  )
target_link_libraries(sisl_logging ${COMMON_DEPS})

add_executable(logging_example)
target_sources(logging_example PRIVATE
    test/example.cpp
  )
target_link_libraries(logging_example sisl ${COMMON_DEPS})
