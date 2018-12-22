set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPP_FLAGS} ${CPP_WARNINGS} -DPACKAGE_NAME=${CONAN_PACKAGE_NAME} -DPACKAGE_VERSION=${CONAN_PACKAGE_VERSION}")

find_package(Threads REQUIRED)
find_package(Boost 1.64 REQUIRED)

include_directories(BEFORE .)
include_directories(BEFORE ..)

set(TEST_THREAD_BUFFER tests/test_thread_buffer.cpp)
add_executable(test_thread_buffer ${TEST_THREAD_BUFFER})
target_link_libraries(test_thread_buffer ${Boost_LIBRARIES} ${CONAN_LIBS})
add_test(NAME ThreadBufferTest COMMAND test_thread_buffer)
