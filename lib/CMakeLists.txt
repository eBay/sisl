cmake_minimum_required (VERSION 3.11)

set(CMAKE_CXX_STANDARD 17)

add_library(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE
    rpc_server.cpp
    rpc_client.cpp
  )
target_link_libraries(${PROJECT_NAME}
    gRPC::grpc++
    sisl::sisl
    Boost::Boost
  )
