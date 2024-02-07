cmake_minimum_required (VERSION 3.11)

include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR}/../auth_manager)

add_library(sisl_grpc)
target_sources(sisl_grpc PRIVATE
  rpc_server.cpp
  rpc_client.cpp
  )
target_link_libraries(sisl_grpc PUBLIC
  sisl_core
  gRPC::grpc++
  )

if(DEFINED ENABLE_TESTING)
  if(${ENABLE_TESTING})
    add_subdirectory(tests)
  endif()
endif()
