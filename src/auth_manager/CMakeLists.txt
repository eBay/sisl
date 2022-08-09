cmake_minimum_required (VERSION 3.10)

add_flags("-Wno-unused-parameter")

include_directories(BEFORE ..)
include_directories(BEFORE .)

find_package(FlatBuffers REQUIRED)

set(AUTH_MGR_SOURCE_FILES 
    auth_manager.cpp
   )
add_library(sisl_auth_manager OBJECT ${AUTH_MGR_SOURCE_FILES})
target_link_libraries(sisl_auth_manager
    ${COMMON_DEPS}
    cpr::cpr
    flatbuffers::flatbuffers
    jwt-cpp::jwt-cpp
  )
message("Flatbuffers parser: [${FLATBUFFERS_FLATC_EXECUTABLE}]")
settings_gen_cpp(${FLATBUFFERS_FLATC_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/generated/ sisl_auth_manager security_config.fbs)

set(TRF_CLIENT_SOURCE_FILES 
    trf_client.cpp
   )
add_library(sisl_trf_client OBJECT ${TRF_CLIENT_SOURCE_FILES})
target_link_libraries(sisl_trf_client
    ${COMMON_DEPS}
    cpr::cpr
    flatbuffers::flatbuffers
  )
settings_gen_cpp(${FLATBUFFERS_FLATC_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/generated/ sisl_trf_client security_config.fbs)
