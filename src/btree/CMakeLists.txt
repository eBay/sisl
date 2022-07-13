if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    add_flags("-Wno-unused-parameter -Wno-cast-function-type")
endif()

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(TEST_BTREENODE_SOURCE_FILES
    tests/test_btree_node.cpp
   )
add_executable(test_btree_node ${TEST_BTREENODE_SOURCE_FILES})
target_link_libraries(test_btree_node sisl ${CONAN_LIBS})
