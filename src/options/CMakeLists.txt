cmake_minimum_required (VERSION 3.11)

add_library(sisl_options OBJECT)
target_sources(sisl_options PRIVATE
    options.cpp
  )
target_link_libraries(sisl_options ${COMMON_DEPS})

add_executable(basic_test)
target_sources(basic_test PRIVATE
    tests/basic.cpp
  )
target_link_libraries(basic_test sisl ${COMMON_DEPS} GTest::gtest)
add_test(NAME OptionsBasics COMMAND basic_test ${extra_args})
