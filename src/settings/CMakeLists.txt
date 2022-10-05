cmake_minimum_required (VERSION 3.10)

if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-attributes") # needed for C++ 20 folly compilation
endif()

include_directories(BEFORE ..)
include_directories(BEFORE .)
include_directories(BEFORE . ${CMAKE_CURRENT_SOURCE_DIR}/)

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
set(FLATBUFFERS_FLATC_EXECUTABLE, ${CONAN_BIN_DIRS_FLATBUFFER}/flatc)
settings_gen_cpp(${FLATBUFFERS_FLATC_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/generated/ test_settings tests/test_app_schema.fbs)
target_link_libraries(test_settings sisl ${COMMON_DEPS} flatbuffers::flatbuffers GTest::gtest)
add_test(NAME SettingsTest COMMAND test_settings)
