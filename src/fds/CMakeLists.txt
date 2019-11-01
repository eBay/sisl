set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror -Wno-unused-parameter -Wno-cast-function-type")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPP_FLAGS} ${CPP_WARNINGS} -DPACKAGE_NAME=${CONAN_PACKAGE_NAME} -DPACKAGE_VERSION=${CONAN_PACKAGE_VERSION}")

include_directories(BEFORE .)
include_directories(BEFORE ..)

set(TEST_STREAM_TRACKER_SOURCES tests/test_stream_tracker.cpp)
add_executable(test_stream_tracker ${TEST_STREAM_TRACKER_SOURCES})
target_link_libraries(test_stream_tracker sisl ${CONAN_LIBS})
#add_test(NAME HttpServerTest COMMAND test_http_server)

set(GROUP_COMMIT_SOURCES tests/group_commit.cpp)
add_executable(group_commit ${GROUP_COMMIT_SOURCES})
target_link_libraries(group_commit sisl ${CONAN_LIBS})

set(TEST_BITSET_SOURCES tests/test_bitset.cpp)
add_executable(test_bitset ${TEST_BITSET_SOURCES})
target_link_libraries(test_bitset sisl ${CONAN_LIBS})

set(TEST_BITWORD_SOURCES tests/test_bitword.cpp)
add_executable(test_bitword ${TEST_BITWORD_SOURCES})
target_link_libraries(test_bitword sisl ${CONAN_LIBS})