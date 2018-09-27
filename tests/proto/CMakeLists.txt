find_package(Protobuf REQUIRED)
include(${CMAKE_HOME_DIRECTORY}/cmake/grpc.cmake)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS sds_grpc_test.proto)

PROTOBUF_GENERATE_GRPC_CPP(GRPC_SRCS GRPC_HDRS sds_grpc_test.proto)


MESSAGE( STATUS "PROTO_SRCS = " ${PROTO_SRCS} " " ${PROTO_HDRS})
MESSAGE( STATUS "GRPC_SRCS = " ${GRPC_SRCS} " " ${GRPC_HDRS})


add_library(test_proto ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS})
