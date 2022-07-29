add_flags("-Wno-unused-parameter")

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(WISR_VECTOR_TEST 
    tests/test_wisr_vector.cpp
   )
add_executable(wisr_vector_test ${WISR_VECTOR_TEST})
target_link_libraries(wisr_vector_test ${COMMON_DEPS} benchmark::benchmark GTest::gtest)
add_test(NAME WisrVectorTest COMMAND wisr_vector_test)

set(WISR_VECTOR_BENCHMARK 
    tests/wisr_vector_benchmark.cpp
   )
add_executable(wisr_vector_benchmark ${WISR_VECTOR_BENCHMARK})
target_link_libraries(wisr_vector_benchmark ${COMMON_DEPS} benchmark::benchmark)

set(WISR_LIST_BENCHMARK 
    tests/wisr_list_benchmark.cpp
   )
add_executable(wisr_list_benchmark ${WISR_LIST_BENCHMARK})
target_link_libraries(wisr_list_benchmark ${COMMON_DEPS} benchmark::benchmark)

set(WISR_DEQUE_BENCHMARK 
    tests/wisr_deque_benchmark.cpp
   )
add_executable(wisr_deque_benchmark ${WISR_DEQUE_BENCHMARK})
target_link_libraries(wisr_deque_benchmark ${COMMON_DEPS} benchmark::benchmark)

set(WISR_INTRUSIVE_SLIST_BENCHMARK 
    tests/wisr_intrusive_slist_benchmark.cpp
   )
add_executable(wisr_intrusive_slist_benchmark ${WISR_INTRUSIVE_SLIST_BENCHMARK})
target_link_libraries(wisr_intrusive_slist_benchmark ${COMMON_DEPS} benchmark::benchmark)
