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

if(TARGET sisl_metrics)
  target_link_libraries(sisl_http PUBLIC sisl_metrics)
  target_compile_definitions(sisl_http PUBLIC PROMETHEUS_METRICS_REPORTER)
endif()

add_executable(test_http_server)
target_sources(test_http_server PRIVATE
  tests/test_http_server.cpp
  )
target_link_libraries(test_http_server sisl_http sisl_options GTest::gtest)
add_test(NAME HttpServer COMMAND test_http_server -v warn)
