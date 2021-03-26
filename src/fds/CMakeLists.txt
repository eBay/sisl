if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-unused-parameter -Wno-cast-function-type")
endif()

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(TEST_STREAM_TRACKER_SOURCES 
    tests/test_stream_tracker.cpp
   )
add_executable(test_stream_tracker ${TEST_STREAM_TRACKER_SOURCES})
target_link_libraries(test_stream_tracker sisl ${CONAN_LIBS})
#add_test(NAME HttpServerTest COMMAND test_http_server)

set(GROUP_COMMIT_SOURCES
    tests/group_commit.cpp
   )
add_executable(group_commit ${GROUP_COMMIT_SOURCES})
target_link_libraries(group_commit sisl ${CONAN_LIBS})

set(TEST_BITSET_SOURCES 
    tests/test_bitset.cpp
   )
add_executable(test_bitset ${TEST_BITSET_SOURCES})
target_link_libraries(test_bitset sisl ${CONAN_LIBS})
add_test(NAME bitset COMMAND test_bitset)

set(TEST_BITWORD_SOURCES
    tests/test_bitword.cpp
   )
add_executable(test_bitword ${TEST_BITWORD_SOURCES})
target_link_libraries(test_bitword sisl ${CONAN_LIBS})

if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-attributes") # needed for C++ 20 folly compilation
endif()

set(OBJ_ALLOCATOR_BENCHMARK_FILES 
    tests/obj_allocator_benchmark.cpp
   )
add_executable(obj_allocator_benchmark ${OBJ_ALLOCATOR_BENCHMARK_FILES})
target_link_libraries(obj_allocator_benchmark sisl ${CONAN_LIBS})
add_test(NAME ObjAllocatorBenchmark COMMAND obj_allocator_benchmark)

set(TEST_OBJALLOCATOR_SOURCE_FILES
    tests/test_obj_allocator.cpp
   )
add_executable(test_obj_allocator ${TEST_OBJALLOCATOR_SOURCE_FILES})
target_link_libraries(test_obj_allocator sisl ${CONAN_LIBS})
add_test(NAME ObjAlloc COMMAND test_obj_allocator)

set(TEST_CBMUTEX_SOURCE_FILES
    tests/test_cb_mutex.cpp
   )
add_executable(test_cb_mutex ${TEST_CBMUTEX_SOURCE_FILES})
target_link_libraries(test_cb_mutex sisl ${CONAN_LIBS})
add_test(NAME TestCBMutex COMMAND test_cb_mutex)

set(TEST_HASHNODE_FILES
    tests/test_hashnode.cpp
   )
add_executable(test_hashnode ${TEST_HASHNODE_FILES})
target_link_libraries(test_hashnode sisl ${CONAN_LIBS})
add_test(NAME TestHashNode COMMAND test_hashnode)