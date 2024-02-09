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

add_executable(client_test
      client_test.cpp
  )
target_link_libraries(client_test
    sisl_grpc
    GTest::gmock
    ${COMMON_DEPS}
  )
  add_test(NAME Client_Test COMMAND client_test)
