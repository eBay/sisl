
file(GLOB PROTO_IDLS *.proto)
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${PROTO_IDLS})

list(GET PROTO_HDRS 0 FIRST_PROTO)
get_filename_component(PROTO_DIR ${FIRST_PROTO} DIRECTORY)
set(PROTO_PATH ${PROTO_DIR} PARENT_SCOPE)

set(FLIP_LIB_FILES
          ${PROTO_SRCS}
          ${PROTO_HDRS}
        )
add_library(flip ${FLIP_LIB_FILES})
target_link_libraries(flip ${CONAN_LIBS})
message("Here: ${PROTO_PATH}")
