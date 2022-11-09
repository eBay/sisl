cmake_minimum_required(VERSION 3.10)

if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-unused-parameter -Wno-cast-function-type")
endif()

find_package(gRPC REQUIRED)

add_subdirectory (proto)

add_library(flip
          lib/flip_rpc_server.cpp
          $<TARGET_OBJECTS:flip_proto>
        )
if(NOT ${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(flip BEFORE PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
target_include_directories(flip BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(flip
          sisl
          gRPC::grpc++
          spdlog::spdlog
          nlohmann_json::nlohmann_json
        )

add_executable(test_flip lib/test_flip.cpp)
target_link_libraries(test_flip flip cxxopts::cxxopts)
if(NOT ${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(test_flip BEFORE PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
target_include_directories(test_flip BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME TestFlip COMMAND test_flip)

add_executable(test_flip_server lib/test_flip_server.cpp)
if(NOT ${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(test_flip_server BEFORE PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
target_include_directories(test_flip_server BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_flip_server flip cxxopts::cxxopts)

add_executable(test_flip_local_client client/local/test_flip_local_client.cpp)
if(NOT ${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(test_flip_local_client BEFORE PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
target_include_directories(test_flip_local_client BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_flip_local_client flip cxxopts::cxxopts)
add_test(NAME TestFlipLocalClient COMMAND test_flip_local_client)
