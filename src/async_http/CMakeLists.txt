set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPP_FLAGS} ${CPP_WARNINGS} -DPACKAGE_NAME=${CONAN_PACKAGE_NAME} -DPACKAGE_VERSION=${CONAN_PACKAGE_VERSION}")

find_package(Threads REQUIRED)
find_package(Boost 1.64 REQUIRED)

include_directories(BEFORE .)
include_directories(BEFORE ..)

set(TEST_HTTP_SERVER_SOURCES tests/test_http_server.cpp)
add_executable(test_http_server ${TEST_HTTP_SERVER_SOURCES})
target_link_libraries(test_http_server ${Boost_LIBRARIES} ${CONAN_LIBS})
#add_test(NAME HttpServerTest COMMAND test_http_server)

