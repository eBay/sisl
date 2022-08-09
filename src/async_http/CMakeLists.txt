cmake_minimum_required (VERSION 3.10)

add_flags("-Wno-unused-parameter -Wno-cast-function-type")

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(TEST_HTTP_SERVER_SOURCES 
    tests/test_http_server.cpp
   )
add_executable(test_http_server ${TEST_HTTP_SERVER_SOURCES})
target_link_libraries(test_http_server sisl)

set(TEST_HTTP_SERVER_AUTH_SOURCES 
    tests/AuthTest.cpp
   )
add_executable(test_http_server_auth ${TEST_HTTP_SERVER_AUTH_SOURCES})
target_link_libraries(test_http_server_auth sisl)
add_test(NAME test_http_server_auth COMMAND test_http_server_auth)

