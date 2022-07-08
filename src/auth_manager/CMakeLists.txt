add_flags("-Wno-unused-parameter")

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(AUTH_MGR_SOURCE_FILES 
    auth_manager.cpp
   )
add_library(sisl_auth_manager OBJECT ${AUTH_MGR_SOURCE_FILES})
settings_gen_cpp(${CONAN_BIN_DIRS_FLATBUFFERS} ${CMAKE_CURRENT_BINARY_DIR}/generated/ sisl_auth_manager security_config.fbs)

set(TRF_CLIENT_SOURCE_FILES 
    trf_client.cpp
   )
add_library(sisl_trf_client OBJECT ${TRF_CLIENT_SOURCE_FILES})
settings_gen_cpp(${CONAN_BIN_DIRS_FLATBUFFERS} ${CMAKE_CURRENT_BINARY_DIR}/generated/ sisl_trf_client security_config.fbs)
