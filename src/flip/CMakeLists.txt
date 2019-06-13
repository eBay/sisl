cmake_minimum_required(VERSION 3.10)
project(flip)
include(CMakeScripts/grpc.cmake)

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

file(GLOB PROTO_IDLS proto/*.proto)
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${PROTO_IDLS})

file(GLOB GRPC_IDLS proto/flip_server.proto)
PROTOBUF_GENERATE_GRPC_CPP(GRPC_SRCS GRPC_HDRS ${GRPC_IDLS})

list(GET PROTO_HDRS 0 FIRST_PROTO)
get_filename_component(PROTO_DIR ${FIRST_PROTO} DIRECTORY)
set(PROTO_PATH ${PROTO_DIR})

include_directories(BEFORE include ${PROTO_DIR} src)
set(FLIP_LIB_FILES
          ${PROTO_SRCS}
          ${PROTO_HDRS}
          ${GRPC_SRCS}
          ${GRPC_HDRS}
          src/flip_rpc_server.cpp
        )

add_library(flip ${FLIP_LIB_FILES})
target_link_libraries(flip ${CONAN_LIBS})

add_executable(test_flip src/test_flip.cpp)
target_link_libraries(test_flip flip)

add_executable(test_flip_local_client src/test_flip_local_client.cpp)
target_link_libraries(test_flip_local_client flip)

add_executable(test_flip_server src/test_flip_server.cpp)
target_link_libraries(test_flip_server flip)