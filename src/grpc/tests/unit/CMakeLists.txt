cmake_minimum_required (VERSION 3.12)

add_executable(auth_test
      auth_test.cpp
      $<TARGET_OBJECTS:test_proto>
  )
target_link_libraries(auth_test
    sisl_grpc
    GTest::gmock
  )
add_test(NAME Auth_Test COMMAND auth_test)
set_tests_properties(Auth_Test PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=detect_stack_use_after_scope=0:detect_container_overflow=0:allow_user_poisoning=0")

add_executable(client_test
      client_test.cpp
  )
target_link_libraries(client_test
    sisl_grpc
    GTest::gmock
    ${COMMON_DEPS}
  )
  add_test(NAME Client_Test COMMAND client_test)
