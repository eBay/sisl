cmake_minimum_required (VERSION 3.10)

if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-attributes") # needed for C++ 20 folly compilation
endif()

include_directories(BEFORE ..)
include_directories(BEFORE .)
include_directories(BEFORE . ${CMAKE_CURRENT_BINARY_DIR}/generated/)

find_package(FlatBuffers REQUIRED)

set(SETTINGS_SOURCE_FILES 
    settings.cpp
   )
add_library(sisl_settings OBJECT ${SETTINGS_SOURCE_FILES})
target_link_libraries(sisl_settings
    ${COMMON_DEPS}
    flatbuffers::flatbuffers
  )

set(TEST_SETTINGS_SOURCE_FILES
    tests/test_settings.cpp
   )
add_executable(test_settings ${TEST_SETTINGS_SOURCE_FILES})
settings_gen_cpp(${FLATBUFFERS_FLATC_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/generated/ test_settings tests/test_app_schema.fbs)
target_link_libraries(test_settings sisl ${COMMON_DEPS} flatbuffers::flatbuffers)
add_test(NAME SettingsTest COMMAND test_settings)
add_test(NAME SettingsTestOverride COMMAND test_settings --config_path=${CMAKE_BINARY_DIR} --override_config test_app_schema.config.database.databaseHost:myhost.com)
