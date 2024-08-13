cmake_minimum_required (VERSION 3.11)

add_library(sisl_options)
target_sources(sisl_options PRIVATE
  options.cpp
  )
target_link_libraries(sisl_options PUBLIC
  boost::boost
  cxxopts::cxxopts
  )

add_executable(basic_test)
target_sources(basic_test PRIVATE
  tests/basic.cpp
  )
target_link_libraries(basic_test sisl_options GTest::gtest)
add_test(NAME OptionsBasics COMMAND basic_test)
