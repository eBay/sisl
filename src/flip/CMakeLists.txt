cmake_minimum_required(VERSION 3.7)
project(flip)

set(CMAKE_CXX_STANDARD 17)

#set (PROJECT_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)

if (NOT DEFINED PROTOBUF_PROTOC_EXECUTABLE)
    set(PROTOBUF_PROTOC_EXECUTABLE protoc)
endif()

if (NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH /usr/local/)
endif()

set(PROTO_DIR ${PROJECT_SOURCE_DIR}/proto)
set(PROTO_GEN_DIR ${CMAKE_BINARY_DIR}/gen_src/proto)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})
#set(FLIP_PROTO_FILE proto/flip_spec.proto)

get_filename_component(PROTO_FULL_PATH proto/flip_spec.proto ABSOLUTE)
file(RELATIVE_PATH PROTO_REL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/proto ${PROTO_FULL_PATH})
message(STATUS "Processing ${PROTO_FULL_PATH} Relative path ${PROTO_REL_PATH}")

#set(_GEN_HEADERS src/proto/flip_spec.pb.h)
#list(APPEND _generated_headers "${_GEN_HEADERS}")
#set(_GEN_SOURCES src/proto/flip_spec.pb.cc)

#target_sources(test_flip PRIVATE ${_GEN_SOURCES})
#set_source_files_properties(${_GEN_SOURCES} ${_GEN_HEADERS} PROPERTIES GENERATED TRUE)

add_custom_command(
        OUTPUT ${PROTO_GEN_DIR}/flip_spec.pb.cc
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
        ARGS --cpp_out=${PROTO_GEN_DIR} -I. ${PROTO_REL_PATH}
        DEPENDS proto/flip_spec.proto
        WORKING_DIRECTORY ${PROTO_DIR}
        COMMENT "Running C++ protocol buffer compiler on flip_spec.proto"
        VERBATIM
)

set(FLIP_LIB_FILES
        src/flip.hpp
        ${PROTO_GEN_DIR}/flip_spec.pb.cc
        )

set(TEST_FLIP_FILES
        ${FLIP_LIB_FILES}
        src/test_flip.cpp
    )

set(TEST_FLIP_CLIENT_FILES
        ${FLIP_LIB_FILES}
        src/test_flip_client.cpp
        )

find_library(PROTOBUF_LIBRARY protobuf HINTS ${CMAKE_PREFIX_PATH}/lib)
find_library(GLOG_LIBRARY glog HINTS ${CMAKE_PREFIX_PATH}/lib)
find_library(GFLAGS_LIBRARY gflags HINTS ${CMAKE_PREFIX_PATH}/lib)
include_directories(${CMAKE_PREFIX_PATH}/include ${PROTO_GEN_DIR})

add_library(flip ${FLIP_LIB_FILES})
target_link_libraries(flip ${PROTOBUF_LIBRARY} ${GLOG_LIBRARY} ${GFLAGS_LIBRARY})

add_executable(test_flip ${TEST_FLIP_FILES})
target_link_libraries(test_flip ${PROTOBUF_LIBRARY} ${GLOG_LIBRARY} ${GFLAGS_LIBRARY} pthread boost_system)

add_executable(test_flip_client ${TEST_FLIP_CLIENT_FILES})
target_link_libraries(test_flip_client ${PROTOBUF_LIBRARY} ${GLOG_LIBRARY} ${GFLAGS_LIBRARY} pthread boost_system)

install(TARGETS flip DESTINATION ${CMAKE_PREFIX_PATH}/lib)
install(FILES src/flip.hpp proto/flip_spec.proto ${PROTO_GEN_DIR}/flip_spec.pb.h DESTINATION ${CMAKE_PREFIX_PATH}/include/flip)

