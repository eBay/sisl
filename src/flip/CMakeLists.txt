cmake_minimum_required(VERSION 3.10)
project(flip)

set(CMAKE_CXX_STANDARD 17)

if(EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
    include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
    conan_basic_setup()
else()
    message(WARNING "The file conanbuildinfo.cmake doesn't exist, you have to run conan install first")
    return()
endif()

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
  if (NOT ${CONAN_SETTINGS_COMPILER} STREQUAL "clang" AND NOT ${CONAN_SETTINGS_COMPILER} STREQUAL "apple-clang")
    include (cmake/debug_flags.cmake)
  endif ()
endif ()
if  (${MEMORY_SANITIZER_ON})
  include (cmake/mem_sanitizer.cmake)
endif ()

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif ()

find_package(Protobuf REQUIRED)

add_subdirectory(proto)
message("Here: ${PROTO_PATH}")

include_directories(BEFORE include ${PROTO_PATH})

add_executable(test_flip src/test_flip.cpp)
target_link_libraries(test_flip flip)

add_executable(test_flip_client src/test_flip_client.cpp)
target_link_libraries(test_flip_client flip)
