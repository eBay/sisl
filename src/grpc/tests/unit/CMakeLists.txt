cmake_minimum_required (VERSION 3.10)

add_executable(auth_test
      auth_test.cpp
      $<TARGET_OBJECTS:test_proto>
  )
target_link_libraries(auth_test
    sisl
    sisl_grpc
    Pistache::Pistache
    GTest::gmock
    ${COMMON_DEPS}
  )
add_test(NAME Auth_Test COMMAND auth_test)
