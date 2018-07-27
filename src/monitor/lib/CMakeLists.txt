## build a shared library

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../.)
include_directories(${PROMETHEUS_INCLUDE_DIR})
## for the installed packages: evhtp and glog
include_directories(/usr/local/include)
include_directories(${EVHTP_INCLUDE_DIR}/evhtp)

add_library(numonitor
 ${CMAKE_CURRENT_SOURCE_DIR}/metrics_monitor.cpp
 ${CMAKE_CURRENT_SOURCE_DIR}/histogram_buckets.cpp
 ${CMAKE_CURRENT_SOURCE_DIR}/evhtp_exposer.cpp
 ${CMAKE_CURRENT_SOURCE_DIR}/evhtp_handler.cpp 
 ${CMAKE_CURRENT_SOURCE_DIR}/evhtp_server.cpp
 ${CMAKE_CURRENT_SOURCE_DIR}/expo_format.cpp
 ##${CMAKE_CURRENT_SOURCE_DIR}/metrics_generator.cpp
 ##${CMAKE_CURRENT_SOURCE_DIR}/dbconnectionthread_metrics.cpp
 ##${CMAKE_CURRENT_SOURCE_DIR}/dbtxnprocessing_metrics.cpp
 
)

## make it become a shared library
## target_link_libraries(numonitor ${PROMETHEUS_LIBRARY} /usr/local/lib/libevhtp.a glog)

##set_target_properties (shuffle PROPERTIES VERSION 1.0)

## default to be installed to /usr/local/lib
## install (TARGETS shm_management DESTINATION lib)

