cmake_minimum_required (VERSION 3.10)

if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-unused-parameter -Wno-cast-function-type")
endif()

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
