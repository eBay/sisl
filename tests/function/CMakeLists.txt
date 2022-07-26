
include_directories(${CMAKE_CURRENT_BINARY_DIR}/../proto)

enable_testing()

# build echo_server
add_executable(echo_server
        echo_server.cpp
        $<TARGET_OBJECTS:test_proto>
    )
add_dependencies(echo_server grpc_helper)
target_link_libraries(echo_server grpc_helper ${CONAN_LIBS} )
add_test(NAME Echo_Ping_Server COMMAND echo_server)

# build echo_sync_client
#add_executable(echo_sync_client echo_sync_client.cpp)
#add_dependencies(echo_sync_client grpc_helper test_proto)
#target_link_libraries(echo_sync_client ${FUNCTION_TEST_LIBS} )

# build echo_async_client
add_executable(echo_async_client
        echo_async_client.cpp
        $<TARGET_OBJECTS:test_proto>
    )
add_dependencies(echo_async_client grpc_helper)
target_link_libraries(echo_async_client grpc_helper ${CONAN_LIBS} )
add_test(NAME Echo_Ping_Async_Client_Server COMMAND echo_async_client)
SET_TESTS_PROPERTIES(Echo_Ping_Async_Client_Server PROPERTIES DEPENDS TestHttpSanity)
