cmake_minimum_required (VERSION 3.11)

add_library(sisl_cache)
target_sources(sisl_cache PRIVATE
  lru_evictor.cpp
  )
target_link_libraries(sisl_cache PUBLIC
  sisl_buffer
  )

add_executable(test_range_hashmap)
target_sources(test_range_hashmap PRIVATE
  tests/test_range_hashmap.cpp
  )
target_include_directories(test_range_hashmap BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_range_hashmap sisl_cache GTest::gtest)
#add_test(NAME RangeHashMap COMMAND test_range_hashmap --num_iters 10000)

add_executable(test_range_cache)
target_sources(test_range_cache PRIVATE
  tests/test_range_cache.cpp
  )
target_include_directories(test_range_cache BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_range_cache sisl_cache GTest::gtest)
#add_test(NAME RangeCache COMMAND test_range_cache --num_iters 1000)

add_executable(test_simple_cache)
target_sources(test_simple_cache PRIVATE
  tests/test_simple_cache.cpp
  )
target_include_directories(test_simple_cache BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_simple_cache sisl_cache GTest::gtest)
add_test(NAME SimpleCache COMMAND test_simple_cache --num_iters 1000)
