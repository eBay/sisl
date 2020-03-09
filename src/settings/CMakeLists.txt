include_directories(BEFORE .)
include_directories(BEFORE ..)

set(CMAKE_CXX_STANDARD 17)
#set(SETTINGS_SOURCE_FILES settings.hpp)
#add_library(sisl_settings OBJECT ${SETTINGS_SOURCE_FILES})

#flatbuffers_generate_cpp(${CONAN_BIN_DIRS_FLATBUFFERS} ${CMAKE_CURRENT_SOURCE_DIR}/generated/ sisl::embedded_t sisl_settings tests/test_app_schema.fbs)

set(TEST_SETTINGS_SOURCE_FILES tests/test_settings.cpp)
add_executable(test_settings ${TEST_SETTINGS_SOURCE_FILES})
settings_gen_cpp(${CONAN_BIN_DIRS_FLATBUFFERS} ${CMAKE_CURRENT_SOURCE_DIR}/generated/ test_settings tests/test_app_schema.fbs)
#target_sources(test_settings PRIVATE generated/test_app_schema_bindump.cpp)
#target_link_libraries(test_settings sisl sisl_settings ${CONAN_LIBS})
target_link_libraries(test_settings sisl ${CONAN_LIBS})