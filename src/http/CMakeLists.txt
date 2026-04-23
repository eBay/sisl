cmake_minimum_required(VERSION 3.11)

find_package(Pistache REQUIRED)

add_library(sisl_http)
target_sources(sisl_http PRIVATE
  http_server.cpp
  )
target_link_libraries(sisl_http PUBLIC
  sisl_logging
  Pistache::Pistache
  )

add_executable(test_http_server)
target_sources(test_http_server PRIVATE
  tests/test_http_server.cpp
  )
target_link_libraries(test_http_server sisl_http sisl_options GTest::gtest)
add_test(NAME HttpServer COMMAND test_http_server -v warn)
