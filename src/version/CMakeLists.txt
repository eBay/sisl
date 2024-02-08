cmake_minimum_required (VERSION 3.11)

add_library(sisl_version)
target_sources(sisl_version PRIVATE
    version.cpp
  )
target_link_libraries(sisl_version PUBLIC
    sisl_logging
    zmarok-semver::zmarok-semver
    )

if (DEFINED ENABLE_TESTING)
  if (${ENABLE_TESTING})
    add_executable(test_version)
    target_sources(test_version PRIVATE
        tests/test_version.cpp
      )
    target_link_libraries(test_version sisl_version GTest::gtest)
    add_test(NAME Version COMMAND test_version)
  endif()
endif()
