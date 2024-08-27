cmake_minimum_required (VERSION 3.11)

include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(sisl_metrics OBJECT)
target_sources(sisl_metrics PRIVATE
    metrics.cpp
    metrics_atomic.cpp
    metrics_group_impl.cpp
    metrics_rcu.cpp 
    metrics_tlocal.cpp 
  )
target_link_libraries(sisl_metrics
    ${COMMON_DEPS}
    Folly::folly
  )

add_executable(metrics_farm_test)
target_sources(metrics_farm_test PRIVATE
    tests/farm_test.cpp
  )
target_link_libraries(metrics_farm_test sisl ${COMMON_DEPS} GTest::gtest)
add_test(NAME MetricsFarm COMMAND metrics_farm_test)

add_executable(metrics_wrapper_test)
target_sources(metrics_wrapper_test PRIVATE
    tests/wrapper_test.cpp
  )
target_link_libraries(metrics_wrapper_test sisl ${COMMON_DEPS} GTest::gtest)
add_test(NAME MetricsWrapper COMMAND metrics_wrapper_test)

add_executable(metrics_benchmark)
target_sources(metrics_benchmark PRIVATE
    tests/metrics_benchmark.cpp
  )
target_link_libraries(metrics_benchmark sisl ${COMMON_DEPS} benchmark::benchmark)
add_test(NAME MetricsBenchmark COMMAND metrics_benchmark)
