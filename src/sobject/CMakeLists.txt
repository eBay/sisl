cmake_minimum_required (VERSION 3.11)

add_library(sisl_sobject_mgr OBJECT)
target_sources(sisl_sobject_mgr PRIVATE sobject.cpp)
target_link_libraries(sisl_sobject_mgr ${COMMON_DEPS})

if (DEFINED ENABLE_TESTING)
  if (${ENABLE_TESTING})
    add_executable(test_sobject)
    target_sources(test_sobject PRIVATE
        tests/test_sobject.cpp
      )
    target_link_libraries(test_sobject sisl ${COMMON_DEPS} GTest::gtest)
    add_test(NAME Sobject COMMAND test_sobject)
  endif()
endif()
