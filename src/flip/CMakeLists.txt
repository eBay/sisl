cmake_minimum_required(VERSION 3.10)

if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
  include (cmake/debug_flags.cmake)
endif ()
if  (${MEMORY_SANITIZER_ON})
  include (cmake/mem_sanitizer.cmake)
endif ()

find_package(gRPC REQUIRED)

include_directories(BEFORE include)
include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR}/proto)
include_directories(BEFORE ..)

add_library(flip_server
          src/flip_rpc_server.cpp
          $<TARGET_OBJECTS:flip_proto>
        )
target_link_libraries(flip_server
          sisl::sisl
          gRPC::grpc++
          spdlog::spdlog
        )

add_executable(test_flip src/test_flip.cpp)
target_link_libraries(test_flip flip)

add_executable(test_flip_local_client src/test_flip_local_client.cpp)
target_link_libraries(test_flip_local_client flip)

add_executable(test_flip_server src/test_flip_server.cpp)
target_link_libraries(test_flip_server flip)

add_subdirectory (proto)
