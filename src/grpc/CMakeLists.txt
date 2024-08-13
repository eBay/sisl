cmake_minimum_required (VERSION 3.11)

include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR}/../auth_manager)

add_library(sisl_grpc)
target_sources(sisl_grpc PRIVATE
    rpc_server.cpp
    rpc_client.cpp
    generic_service.cpp
  )
target_link_libraries(sisl_grpc PUBLIC
  sisl_buffer
  gRPC::grpc++
  )

add_subdirectory(tests)
