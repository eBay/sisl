cmake_minimum_required (VERSION 3.10)

add_library(sisl_options OBJECT)
target_sources(sisl_options PRIVATE
    lib/options.cpp
)
target_link_libraries(sisl_options ${COMMON_DEPS})

add_executable(basic_test)
target_sources(basic_test PRIVATE
    tests/basic.cpp
)
target_link_libraries(basic_test sisl ${COMMON_DEPS} GTest::gtest)

if (DEFINED CONAN_BUILD_COVERAGE)
  if (${CONAN_BUILD_COVERAGE})
      list(APPEND extra_args "--gtest_output=xml:/output/test_basic.xml")
  endif ()
endif ()
add_test(NAME BasicTest COMMAND basic_test ${extra_args})
