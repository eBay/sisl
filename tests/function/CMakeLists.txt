
include_directories(${CMAKE_CURRENT_BINARY_DIR}/../proto)

set(FUNCTION_TEST_LIBS sds_grpc test_proto 
    ${CONAN_LIBS_GRPC} 
    ${CONAN_LIBS_PROTOBUF} 
    ${CONAN_LIBS_OPENSSL} 
    ${CONAN_LIBS_C-ARES} 
    ${CONAN_LIBS_ZLIB} )


# build echo_server
add_executable(echo_server echo_server.cpp)
add_dependencies(echo_server sds_grpc test_proto)
target_link_libraries(echo_server ${FUNCTION_TEST_LIBS} )


# build echo_sync_client
add_executable(echo_sync_client echo_sync_client.cpp)
add_dependencies(echo_sync_client sds_grpc test_proto)
target_link_libraries(echo_sync_client ${FUNCTION_TEST_LIBS} )


# build echo_async_client
add_executable(echo_async_client echo_async_client.cpp)
add_dependencies(echo_async_client sds_grpc test_proto)
target_link_libraries(echo_async_client ${FUNCTION_TEST_LIBS} )

