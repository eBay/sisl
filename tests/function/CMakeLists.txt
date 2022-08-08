cmake_minimum_required (VERSION 3.11)

# build echo_server
add_executable(echo_server
      echo_server.cpp
      $<TARGET_OBJECTS:test_proto>
  )
target_link_libraries(echo_server
    grpc_helper
    GTest::gtest
  )
add_test(NAME Echo_Ping_Server COMMAND echo_server)

# build echo_async_client
add_executable(echo_async_client
      echo_async_client.cpp
      $<TARGET_OBJECTS:test_proto>
  )
target_link_libraries(echo_async_client
    grpc_helper
    GTest::gtest
  )
add_test(NAME Echo_Ping_Async_Client_Server COMMAND echo_async_client)
