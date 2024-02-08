cmake_minimum_required (VERSION 3.11)

add_library(sisl_sobject)
target_sources(sisl_sobject PRIVATE
  sobject.cpp
  )
target_link_libraries(sisl_sobject PUBLIC
  sisl_logging
  nlohmann_json::nlohmann_json
  )

if (DEFINED ENABLE_TESTING)
  if (${ENABLE_TESTING})
    add_executable(test_sobject)
    target_sources(test_sobject PRIVATE
        tests/test_sobject.cpp
      )
    target_link_libraries(test_sobject sisl_sobject GTest::gtest)
    add_test(NAME Sobject COMMAND test_sobject)
  endif()
endif()
