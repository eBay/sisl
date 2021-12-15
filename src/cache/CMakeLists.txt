if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-unused-parameter -Wno-cast-function-type")
endif()

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(CACHE_SOURCE_FILES 
    lru_evictor.cpp
   )
add_library(sisl_cache OBJECT ${CACHE_SOURCE_FILES})

set(TEST_RANGEHASH_SOURCE_FILES
    tests/test_range_hashmap.cpp
   )
add_executable(test_range_hashmap ${TEST_RANGEHASH_SOURCE_FILES})
target_link_libraries(test_range_hashmap sisl ${CONAN_LIBS})

set(TEST_RANGECACHE_SOURCE_FILES
    tests/test_range_cache.cpp
   )
add_executable(test_range_cache ${TEST_RANGECACHE_SOURCE_FILES})
target_link_libraries(test_range_cache sisl ${CONAN_LIBS})