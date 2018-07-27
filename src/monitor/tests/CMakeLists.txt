##include_directories(${PROJECT_SOURCE_DIR}/third-party/gtest/include)

#SET(CMAKE_SKIP_BUILD_RPATH  FALSE)
#SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE) 
#SET(CMAKE_INSTALL_RPATH "build/src/alps")
#SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)

find_package (gtest REQUIRED)

#to add glog include in "deps_prefix"
include_directories(${GLOG_INCLUDE_DIR})

include_directories(${GTEST_INCLUDE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/monitor)

include_directories(${EVHTP_INCLUDE_DIR}/evhtp)

function (handle_common_unit_tests targetname srcname binname)
  add_executable(${targetname} ${CMAKE_CURRENT_SOURCE_DIR}/${srcname}.cpp)
  set_target_properties(${targetname} PROPERTIES OUTPUT_NAME ${binname})
  target_link_libraries(${targetname} ${GTEST_LIBRARY})
  add_test(NAME ${targetname} COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${binname})
  install (TARGETS ${targetname} DESTINATION /tmp/bin/test/unit COMPONENT test)
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)

