cmake_minimum_required(VERSION 3.10)
project(flip)
set(CMAKE_CXX_STANDARD 17)

if(EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
    include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
else()
    message(WARNING "Conan Build file does not exist, trying to build without!")
endif()

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
  include (cmake/debug_flags.cmake)
endif ()
if  (${MEMORY_SANITIZER_ON})
  include (cmake/mem_sanitizer.cmake)
endif ()

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif ()

find_package(gRPC CONFIG REQUIRED)
find_package(sisl CONFIG REQUIRED)

include_directories(BEFORE include)
include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR}/proto)

add_library(flip
          src/flip_rpc_server.cpp
          $<TARGET_OBJECTS:flip_proto>
        )
target_link_libraries(flip
          sisl::sisl
          gRPC::grpc++
        )

add_executable(test_flip src/test_flip.cpp)
target_link_libraries(test_flip flip)

add_executable(test_flip_local_client src/test_flip_local_client.cpp)
target_link_libraries(test_flip_local_client flip)

add_executable(test_flip_server src/test_flip_server.cpp)
target_link_libraries(test_flip_server flip)

add_subdirectory (proto)
