cmake_minimum_required (VERSION 3.11)

find_package(flatbuffers REQUIRED)

add_library(sisl_settings OBJECT)
target_sources(sisl_settings PRIVATE
    settings.cpp
  )
target_link_libraries(sisl_settings
    ${COMMON_DEPS}
    flatbuffers::flatbuffers
  )

add_executable(test_settings)
target_sources(test_settings PRIVATE
    tests/test_settings.cpp
  )
settings_gen_cpp(
    ${FLATBUFFERS_FLATC_EXECUTABLE}
    ${CMAKE_CURRENT_BINARY_DIR}/generated/
    test_settings
    tests/test_app_schema.fbs
  )
if(NOT ${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(test_settings BEFORE PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
target_include_directories(test_settings BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_settings sisl ${COMMON_DEPS} flatbuffers::flatbuffers GTest::gtest)
add_test(NAME Settings COMMAND test_settings)
