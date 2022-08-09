cmake_minimum_required (VERSION 3.10)

if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-attributes") # needed for C++ 20 folly compilation
endif()

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(METRICS_SOURCE_FILES 
    metrics.cpp
    metrics_atomic.cpp
    metrics_group_impl.cpp
    metrics_rcu.cpp 
    metrics_tlocal.cpp 
   )
add_library(sisl_metrics OBJECT ${METRICS_SOURCE_FILES})
target_link_libraries(sisl_metrics
    ${COMMON_DEPS}
    Folly::Folly
  )

set(FARM_TEST_SOURCES tests/farm_test.cpp)
add_executable(metrics_farm_test ${FARM_TEST_SOURCES})
target_link_libraries(metrics_farm_test sisl ${COMMON_DEPS} GTest::gtest)
add_test(NAME MetricsFarmTest COMMAND metrics_farm_test)

set(WRAPPER_TEST_SOURCES tests/wrapper_test.cpp)
add_executable(metrics_wrapper_test ${WRAPPER_TEST_SOURCES})
target_link_libraries(metrics_wrapper_test sisl ${COMMON_DEPS} GTest::gtest)
add_test(NAME MetricsWrapperTest COMMAND metrics_wrapper_test)

set(METRICS_BENCHMARK_SOURCES tests/metrics_benchmark.cpp)
add_executable(metrics_benchmark ${METRICS_BENCHMARK_SOURCES})
target_link_libraries(metrics_benchmark sisl ${COMMON_DEPS} benchmark::benchmark)
add_test(NAME MetricsBenchmarkTest COMMAND metrics_benchmark)
