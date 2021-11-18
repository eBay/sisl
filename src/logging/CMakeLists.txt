if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-unused-parameter -Wno-cast-function-type")
endif()

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(LOGGING_SOURCE_FILES 
    lib/backtrace.cpp
    lib/logging.cpp
    lib/stacktrace.cpp
   )
add_library(sisl_logging OBJECT ${LOGGING_SOURCE_FILES})

set(TEST_LOGGING_FILES
    test/example.cpp
   )
add_executable(logging_example ${TEST_LOGGING_FILES})
target_link_libraries(logging_example sisl dl ${CONAN_LIBS})