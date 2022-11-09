cmake_minimum_required (VERSION 3.10)

if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-unused-parameter -Wno-cast-function-type")
endif()

include_directories(BEFORE .)

set(BUFFER_SOURCE_FILES 
    buffer.cpp
   )
add_library(sisl_buffer OBJECT ${BUFFER_SOURCE_FILES})
target_link_libraries(sisl_buffer ${COMMON_DEPS})

set(TEST_STREAM_TRACKER_SOURCES 
    tests/test_stream_tracker.cpp
   )
add_executable(test_stream_tracker ${TEST_STREAM_TRACKER_SOURCES})
target_link_libraries(test_stream_tracker sisl ${COMMON_DEPS} GTest::gtest)
#add_test(NAME HttpServerTest COMMAND test_http_server)

set(TEST_ATOMIC_STATUS_COUNTER_SOURCES 
    tests/test_atomic_status_counter.cpp
   )
add_executable(test_atomic_status_counter ${TEST_ATOMIC_STATUS_COUNTER_SOURCES})
target_link_libraries(test_atomic_status_counter sisl ${COMMON_DEPS} GTest::gtest atomic)
add_test(NAME atomic_status_counter COMMAND test_atomic_status_counter)

set(TEST_BITSET_SOURCES 
    tests/test_bitset.cpp
   )
add_executable(test_bitset ${TEST_BITSET_SOURCES})
target_link_libraries(test_bitset sisl ${COMMON_DEPS} GTest::gtest)
add_test(NAME bitset COMMAND test_bitset)

set(TEST_BITWORD_SOURCES
    tests/test_bitword.cpp
   )
add_executable(test_bitword ${TEST_BITWORD_SOURCES})
target_link_libraries(test_bitword sisl ${COMMON_DEPS} GTest::gtest)

if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-attributes") # needed for C++ 20 folly compilation
endif()

set(OBJ_ALLOCATOR_BENCHMARK_FILES 
    tests/obj_allocator_benchmark.cpp
   )
add_executable(obj_allocator_benchmark ${OBJ_ALLOCATOR_BENCHMARK_FILES})
target_link_libraries(obj_allocator_benchmark sisl ${COMMON_DEPS} benchmark::benchmark)
add_test(NAME ObjAllocatorBenchmark COMMAND obj_allocator_benchmark)

set(TEST_OBJALLOCATOR_SOURCE_FILES
    tests/test_obj_allocator.cpp
   )
add_executable(test_obj_allocator ${TEST_OBJALLOCATOR_SOURCE_FILES})
target_link_libraries(test_obj_allocator sisl ${COMMON_DEPS})
add_test(NAME ObjAlloc COMMAND test_obj_allocator)

set(TEST_CBMUTEX_SOURCE_FILES
    tests/test_cb_mutex.cpp
   )
add_executable(test_cb_mutex ${TEST_CBMUTEX_SOURCE_FILES})
target_link_libraries(test_cb_mutex sisl ${COMMON_DEPS} GTest::gtest)
#add_test(NAME TestCBMutex COMMAND test_cb_mutex)

if (DEFINED MALLOC_IMPL)
    if (${MALLOC_IMPL} STREQUAL "jemalloc")
        set(TEST_JEMALLOC_SOURCE_FILES
            tests/test_jemalloc_helper.cpp
           )
        add_executable(test_jemalloc ${TEST_JEMALLOC_SOURCE_FILES})
        target_link_libraries(test_jemalloc sisl ${COMMON_DEPS} jemalloc GTest::gtest)
        add_test(NAME TestJemalloc COMMAND test_jemalloc)
    elseif (${MALLOC_IMPL} STREQUAL "tcmalloc")
        set(TEST_TCMALLOC_SOURCE_FILES
            tests/test_tcmalloc_helper.cpp
            )
        add_executable(test_tcmalloc ${TEST_TCMALLOC_SOURCE_FILES})
        target_link_libraries(test_tcmalloc sisl ${COMMON_DEPS} GTest::gtest)
        add_test(NAME TestTcmalloc COMMAND test_tcmalloc)
    endif()
endif()
