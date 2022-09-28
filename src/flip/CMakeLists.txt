cmake_minimum_required(VERSION 3.10)

if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-unused-parameter -Wno-cast-function-type")
endif()

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
include_directories(BEFORE .)

add_subdirectory (proto)

add_library(flip
          lib/flip_rpc_server.cpp
          $<TARGET_OBJECTS:flip_proto>
        )
target_link_libraries(flip
          sisl
          gRPC::grpc++
          spdlog::spdlog
          nlohmann_json::nlohmann_json
        )

add_executable(test_flip lib/test_flip.cpp)
target_link_libraries(test_flip flip cxxopts::cxxopts)
add_test(NAME TestFlip COMMAND test_flip)

add_executable(test_flip_server lib/test_flip_server.cpp)
target_link_libraries(test_flip_server flip cxxopts::cxxopts)

add_executable(test_flip_local_client client/local/test_flip_local_client.cpp)
target_link_libraries(test_flip_local_client flip cxxopts::cxxopts)



