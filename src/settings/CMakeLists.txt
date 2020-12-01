add_flags("-Wno-attributes") # needed for C++ 20 folly compilation

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(TEST_SETTINGS_SOURCE_FILES
    tests/test_settings.cpp
   )
add_executable(test_settings ${TEST_SETTINGS_SOURCE_FILES})
settings_gen_cpp(${CONAN_BIN_DIRS_FLATBUFFERS} ${CMAKE_CURRENT_SOURCE_DIR}/generated/ test_settings tests/test_app_schema.fbs)
target_link_libraries(test_settings sisl ${CONAN_LIBS})
add_test(NAME SettingsTest COMMAND test_settings)