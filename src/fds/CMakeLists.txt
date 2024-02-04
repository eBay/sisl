cmake_minimum_required (VERSION 3.11)

include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(sisl_buffer OBJECT)
target_sources(sisl_buffer PRIVATE
    buffer.cpp
  )
target_link_libraries(sisl_buffer folly::folly ${COMMON_DEPS})

if (DEFINED ENABLE_TESTING)
  if (${ENABLE_TESTING})
    add_executable(test_stream_tracker)
    target_sources(test_stream_tracker PRIVATE
        tests/test_stream_tracker.cpp
      )
    target_link_libraries(test_stream_tracker sisl ${COMMON_DEPS} GTest::gtest)
    add_test(NAME StreamTracker COMMAND test_stream_tracker)

    add_executable(test_atomic_status_counter)
    target_sources(test_atomic_status_counter PRIVATE
        tests/test_atomic_status_counter.cpp
      )
    target_link_libraries(test_atomic_status_counter sisl ${COMMON_DEPS} GTest::gtest atomic)
    add_test(NAME AtomicStatusCounter COMMAND test_atomic_status_counter)

    add_executable(test_bitset)
    target_sources(test_bitset PRIVATE
        tests/test_bitset.cpp
      )
    target_link_libraries(test_bitset sisl ${COMMON_DEPS} GTest::gtest)
    add_test(NAME Bitset COMMAND test_bitset)

    add_executable(test_bitword)
    target_sources(test_bitword PRIVATE
        tests/test_bitword.cpp
      )
    target_link_libraries(test_bitword sisl ${COMMON_DEPS} GTest::gtest)
    add_test(NAME Bitword COMMAND test_bitset)

    add_executable(test_compact_bitset)
    target_sources(test_compact_bitset PRIVATE
        tests/test_compact_bitset.cpp
      )
    target_link_libraries(test_compact_bitset sisl ${COMMON_DEPS} GTest::gtest)
    add_test(NAME CompactBitset COMMAND test_compact_bitset)

    add_executable(test_concurrent_insert_vector)
    target_sources(test_concurrent_insert_vector PRIVATE
        tests/test_concurrent_insert_vector.cpp
      )
    target_link_libraries(test_concurrent_insert_vector sisl ${COMMON_DEPS} GTest::gtest)
    add_test(NAME ConcurrentInsertVector COMMAND test_concurrent_insert_vector)

    add_executable(concurrent_insert_vector_bench)
    target_sources(concurrent_insert_vector_bench PRIVATE
        tests/concurrent_insert_vector_bench.cpp
      )
    target_link_libraries(concurrent_insert_vector_bench sisl ${COMMON_DEPS} benchmark::benchmark)
    add_test(NAME ConcurrentVectorBench COMMAND concurrent_insert_vector_bench)

    add_executable(obj_allocator_benchmark)
    target_sources(obj_allocator_benchmark PRIVATE
        tests/obj_allocator_benchmark.cpp
      )
    target_link_libraries(obj_allocator_benchmark sisl ${COMMON_DEPS} benchmark::benchmark)
    add_test(NAME ObjAllocatorBenchmark COMMAND obj_allocator_benchmark)

    add_executable(test_obj_allocator)
    target_sources(test_obj_allocator PRIVATE
        tests/test_obj_allocator.cpp
      )
    target_link_libraries(test_obj_allocator sisl ${COMMON_DEPS})
    add_test(NAME ObjAlloc COMMAND test_obj_allocator)

    add_executable(test_cb_mutex)
    target_sources(test_cb_mutex PRIVATE
        tests/test_cb_mutex.cpp
      )
    target_link_libraries(test_cb_mutex sisl ${COMMON_DEPS} GTest::gtest)
    #add_test(NAME TestCBMutex COMMAND test_cb_mutex)

    add_executable(test_sg_list)
    target_sources(test_sg_list PRIVATE
        tests/test_sg_list.cpp
      )
    target_link_libraries(test_sg_list sisl ${COMMON_DEPS} GTest::gtest)
    add_test(NAME SgList COMMAND test_sg_list)


    if (DEFINED MALLOC_IMPL)
        if (${MALLOC_IMPL} STREQUAL "jemalloc")
            add_executable(test_jemalloc)
            target_sources(test_jemalloc PRIVATE
                tests/test_jemalloc_helper.cpp
              )
            target_link_libraries(test_jemalloc sisl ${COMMON_DEPS} GTest::gtest)
            add_test(NAME TestJemalloc COMMAND test_jemalloc)
        elseif (${MALLOC_IMPL} STREQUAL "tcmalloc")
            add_executable(test_tcmalloc)
            target_sources(test_tcmalloc PRIVATE
                tests/test_tcmalloc_helper.cpp
              )
            target_link_libraries(test_tcmalloc sisl ${COMMON_DEPS} GTest::gtest)
            add_test(NAME TestTcmalloc COMMAND test_tcmalloc)
        endif()
    endif()
  endif()
endif()
