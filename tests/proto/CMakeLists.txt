
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS grpc_helper_test.proto)

PROTOBUF_GENERATE_GRPC_CPP(GRPC_SRCS GRPC_HDRS grpc_helper_test.proto)


MESSAGE( STATUS "PROTO_SRCS = " ${PROTO_SRCS} " " ${PROTO_HDRS})
MESSAGE( STATUS "GRPC_SRCS = " ${GRPC_SRCS} " " ${GRPC_HDRS})


add_library(test_proto ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS})
