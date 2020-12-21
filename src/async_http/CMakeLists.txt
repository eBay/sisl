add_flags("-Wno-unused-parameter -Wno-cast-function-type")

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(TEST_HTTP_SERVER_SOURCES 
    tests/test_http_server.cpp
   )
add_executable(test_http_server ${TEST_HTTP_SERVER_SOURCES})
target_link_libraries(test_http_server ${Boost_LIBRARIES} ${CONAN_LIBS})
#add_test(NAME HttpServerTest COMMAND test_http_server)

