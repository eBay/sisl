include(../CMakeScripts/grpc.cmake)

file(GLOB PROTO_IDLS *.proto)
message("PROTO_IDLS = " ${PROTO_IDLS})
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${PROTO_IDLS})

file(GLOB GRPC_IDLS flip_server.proto)
message("GRPC_IDLS = " ${GRPC_IDLS})
PROTOBUF_GENERATE_GRPC_CPP(GRPC_SRCS GRPC_HDRS ${GRPC_IDLS})

list(GET PROTO_HDRS 0 FIRST_PROTO)
get_filename_component(PROTO_DIR ${FIRST_PROTO} DIRECTORY)
set(PROTO_PATH ${PROTO_DIR} PARENT_SCOPE)

include_directories(BEFORE include ${PROTO_DIR})
set(FLIP_LIB_FILES
          ${FLIP_LIB_FILES}
          ${PROTO_SRCS}
          ${PROTO_HDRS}
          ${GRPC_SRCS}
          ${GRPC_HDRS}
        )

message("FLIP_LIB_FILES = " ${FLIP_LIB_FILES})
message("PROTO_DIR = " ${PROTO_DIR})
add_library(flip ${FLIP_LIB_FILES})
target_link_libraries(flip ${CONAN_LIBS})
