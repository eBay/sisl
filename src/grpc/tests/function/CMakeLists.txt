cmake_minimum_required (VERSION 3.11)

# build echo_server
add_executable(echo_server)
target_sources(echo_server PRIVATE
      echo_server.cpp
      $<TARGET_OBJECTS:test_proto>
  )
target_link_libraries(echo_server
    sisl_grpc
    GTest::gtest
  )
add_test(NAME Echo_Ping_Server COMMAND echo_server)
if (DEFINED ADDRESS_SANITIZER_ON AND ADDRESS_SANITIZER_ON)
    set_tests_properties(Echo_Ping_Server PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_stack_use_after_scope=0:detect_container_overflow=0:allow_user_poisoning=0")
elseif (DEFINED THREAD_SANITIZER_ON AND THREAD_SANITIZER_ON)
    set_tests_properties(Echo_Ping_Server PROPERTIES
        ENVIRONMENT "TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tsan.supp")
endif()

# build echo_async_client
add_executable(echo_async_client)
target_sources(echo_async_client PRIVATE
      echo_async_client.cpp
      $<TARGET_OBJECTS:test_proto>
  )
target_link_libraries(echo_async_client
    sisl_grpc
    GTest::gtest
  )
add_test(NAME Echo_Ping_Async_Client_Server COMMAND echo_async_client)
if (DEFINED ADDRESS_SANITIZER_ON AND ADDRESS_SANITIZER_ON)
    set_tests_properties(Echo_Ping_Async_Client_Server PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_stack_use_after_scope=0:detect_container_overflow=0:allow_user_poisoning=0")
elseif (DEFINED THREAD_SANITIZER_ON AND THREAD_SANITIZER_ON)
    # Skipped under TSAN: all assertions pass but gRPC internals produce
    # false-positive races that cannot be suppressed without broadening
    # suppressions to generic STL functions.
    # sisl gRPC logic is exercised by Echo_Ping_Server and Flip tests.
    set_tests_properties(Echo_Ping_Async_Client_Server PROPERTIES DISABLED TRUE)
endif()
