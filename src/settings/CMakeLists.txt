if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-attributes") # needed for C++ 20 folly compilation
endif()

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(SETTINGS_SOURCE_FILES 
    settings.cpp
   )
add_library(sisl_settings OBJECT ${SETTINGS_SOURCE_FILES})

set(TEST_SETTINGS_SOURCE_FILES
    tests/test_settings.cpp
   )
add_executable(test_settings ${TEST_SETTINGS_SOURCE_FILES})
settings_gen_cpp(${CONAN_BIN_DIRS_FLATBUFFERS} ${CMAKE_CURRENT_SOURCE_DIR}/generated/ test_settings tests/test_app_schema.fbs)
target_link_libraries(test_settings sisl ${CONAN_LIBS})
add_test(NAME SettingsTest COMMAND test_settings)
add_test(NAME SettingsTestOverride COMMAND test_settings --config_path=/tmp --override_config test_app_schema.config.database.databaseHost:myhost.com)
