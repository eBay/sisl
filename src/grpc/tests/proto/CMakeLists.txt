cmake_minimum_required(VERSION 3.11)

add_library(test_proto OBJECT)
target_sources(test_proto PRIVATE
  grpc_helper_test.proto
  )
target_link_libraries(test_proto PUBLIC
  gRPC::grpc++
  )

protobuf_generate(LANGUAGE cpp TARGET test_proto PROTOS)
protobuf_generate(
  TARGET test_proto
  LANGUAGE grpc
  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
  PLUGIN protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
  )
