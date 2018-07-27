# monitor package building and testing

cmake_minimum_required(VERSION 2.8)
project(numonitor)

set(MONITOR_PROJECT_VERSION 0.2.0)

set(CMAKE_MODULE_PATH $(CMAKE_MODULE_PATH) ${PROJECT_SOURCE_DIR}/cmake)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -march=native -Wall -O3")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")

##set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

# third-party libraries
# set(GTEST_ROOT ${PROJECT_SOURCE_DIR}/third-party/gtest)
# add_subdirectory(third-party)

find_package (Protobuffer REQUIRED)

find_package (glog REQUIRED)

find_package (prometheus-cpp REQUIRED)
find_package (evhtp REQUIRED)
find_package (event REQUIRED)


add_subdirectory(lib)

## enable unit testing
if ( UNITTEST )
  enable_testing()
  add_subdirectory(tests)
endif()

## packaging later
## 
