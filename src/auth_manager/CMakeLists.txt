cmake_minimum_required (VERSION 3.10)

find_package(flatbuffers REQUIRED)
find_package(Pistache REQUIRED)

add_library(sisl_auth_manager OBJECT)
target_sources(sisl_auth_manager PRIVATE
    auth_manager.cpp
    trf_client.cpp
  )
if(NOT ${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(sisl_auth_manager BEFORE PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
target_include_directories(sisl_auth_manager BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
if(NOT ${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(test_auth_mgr BEFORE PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
target_include_directories(test_auth_mgr BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_auth_mgr
    sisl
    ${COMMON_DEPS}
    cpr::cpr
    pistache::pistache
    flatbuffers::flatbuffers
    jwt-cpp::jwt-cpp
    GTest::gmock
  )
add_test(NAME test_auth_mgr COMMAND test_auth_mgr)
