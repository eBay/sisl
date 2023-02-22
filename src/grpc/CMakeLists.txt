cmake_minimum_required (VERSION 3.11)

find_package(flatbuffers REQUIRED)
find_package(gRPC REQUIRED)

set(CMAKE_CXX_STANDARD 17)

include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR}/../auth_manager)

add_library(sisl_grpc OBJECT)
target_sources(sisl_grpc PRIVATE
    rpc_server.cpp
    rpc_client.cpp
  )
target_link_libraries(sisl_grpc
    sisl_auth_manager
    gRPC::grpc++
    cpr::cpr
    flatbuffers::flatbuffers
    jwt-cpp::jwt-cpp
    ${COMMON_DEPS}
  )

add_subdirectory(tests)
