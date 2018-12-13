set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPP_FLAGS} ${CPP_WARNINGS} -DPACKAGE_NAME=${CONAN_PACKAGE_NAME} -DPACKAGE_VERSION=${CONAN_PACKAGE_VERSION} -DRCU_SIGNAL")

find_package (Threads REQUIRED)

include_directories(BEFORE .)

set(FUNCTIONALITY_TEST_SOURCES tests/functionality_test.cpp)
add_executable(functionality_test ${FUNCTIONALITY_TEST_SOURCES})
target_link_libraries(functionality_test ${CONAN_LIBS})
add_test(NAME FunctionalityTest COMMAND functionality_test)

set(FARM_TEST_SOURCES tests/farm_test.cpp)
add_executable(farm_test ${FARM_TEST_SOURCES})
target_link_libraries(farm_test ${CONAN_LIBS})
add_test(NAME FarmTest COMMAND farm_test)

set(WRAPPER_TEST_SOURCES tests/wrapper_test.cpp)
add_executable(wrapper_test ${WRAPPER_TEST_SOURCES})
target_link_libraries(wrapper_test ${CONAN_LIBS})
add_test(NAME WrapperTest COMMAND wrapper_test)

set(METRICS_BENCHMARK_SOURCES tests/metrics_benchmark.cpp)
add_executable(metrics_benchmark ${METRICS_BENCHMARK_SOURCES})
target_link_libraries(metrics_benchmark ${CONAN_LIBS})
add_test(NAME BenchmarkTest COMMAND metrics_benchmark)
