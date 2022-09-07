cmake_minimum_required (VERSION 3.10)

find_package(FlatBuffers REQUIRED)
find_package(evhtp REQUIRED)

add_flags("-Wno-unused-parameter -Wno-cast-function-type")

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(AUTH_DEPS
      sisl
      ${COMMON_DEPS}
      evhtp::evhtp
      cpr::cpr
      flatbuffers::flatbuffers
      jwt-cpp::jwt-cpp
      GTest::gmock
    )

add_executable(test_http_server_auth
    tests/AuthTest.cpp
  )
target_link_libraries(test_http_server_auth ${AUTH_DEPS})
add_test(NAME test_http_server_auth COMMAND test_http_server_auth)
