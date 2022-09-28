cmake_minimum_required(VERSION 3.11)

add_library(flip_proto OBJECT
    flip_server.proto
    flip_spec.proto
  )
target_link_libraries(flip_proto
      protobuf::libprotobuf
      gRPC::grpc++
  )

protobuf_generate(LANGUAGE cpp TARGET flip_proto PROTOS flip_spec.proto)
protobuf_generate(LANGUAGE cpp TARGET flip_proto PROTOS flip_server.proto)
protobuf_generate(
    TARGET flip_proto
    LANGUAGE grpc
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    PLUGIN protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
)
