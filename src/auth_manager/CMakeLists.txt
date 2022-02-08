add_flags("-Wno-unused-parameter")

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(AUTH_MGR_SOURCE_FILES 
    auth_manager.cpp
   )
add_library(sisl_auth_manager OBJECT ${AUTH_MGR_SOURCE_FILES})

set(TRF_CLIENT_SOURCE_FILES 
    trf_client.cpp
   )
add_library(sisl_trf_client OBJECT ${TRF_CLIENT_SOURCE_FILES})