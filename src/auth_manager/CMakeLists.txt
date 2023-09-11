cmake_minimum_required (VERSION 3.11)

find_package(flatbuffers REQUIRED)
find_package(Pistache REQUIRED)

if(NOT ${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR})
endif()
include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(sisl_auth_manager OBJECT)
target_sources(sisl_auth_manager PRIVATE
    auth_manager.cpp
    trf_client.cpp
  )
target_link_libraries(sisl_auth_manager
    ${COMMON_DEPS}
    cpr::cpr
    flatbuffers::flatbuffers
    jwt-cpp::jwt-cpp
  )
settings_gen_cpp(
    ${FLATBUFFERS_FLATC_EXECUTABLE}
    ${CMAKE_CURRENT_BINARY_DIR}/generated/
    sisl_auth_manager
    security_config.fbs
  )

add_executable(test_auth_mgr)
target_sources(test_auth_mgr PRIVATE
    tests/AuthTest.cpp
  )
target_link_libraries(test_auth_mgr
    sisl
    ${COMMON_DEPS}
    cpr::cpr
    Pistache::Pistache
    flatbuffers::flatbuffers
    jwt-cpp::jwt-cpp
    GTest::gmock
  )
add_test(NAME AuthManager COMMAND test_auth_mgr)

add_executable(test_lru_cache)
target_sources(test_lru_cache PRIVATE
    tests/LRUCacheTest.cpp
  )
target_link_libraries(test_lru_cache
    sisl
    ${COMMON_DEPS}
    GTest::gmock
  )
add_test(NAME LRUCache COMMAND test_lru_cache)
