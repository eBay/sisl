cmake_minimum_required (VERSION 3.11)

find_package(flatbuffers QUIET REQUIRED)
find_package(gRPC QUIET REQUIRED)

include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR}/../auth_manager)

add_library(sisl_grpc OBJECT)
target_sources(sisl_grpc PRIVATE
    rpc_server.cpp
    rpc_client.cpp
  )
target_link_libraries(sisl_grpc
    gRPC::grpc++
    flatbuffers::flatbuffers
    ${COMMON_DEPS}
  )
