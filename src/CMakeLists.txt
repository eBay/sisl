set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPP_FLAGS} ${CPP_WARNINGS} -DPACKAGE_NAME=${CONAN_PACKAGE_NAME} -DPACKAGE_VERSION=${CONAN_PACKAGE_VERSION}")

find_package (Threads REQUIRED)

include_directories(BEFORE .)

set(FUNCTIONALITY_TEST_SOURCES tests/functionality_test.cpp)
add_executable(functionality_test ${FUNCTIONALITY_TEST_SOURCES})
target_link_libraries(functionality_test ${CONAN_LIBS})
add_test(NAME FunctionalityTest COMMAND functionality_test --gtest_out xml:test_func.xml)

set(LOAD_TEST_SOURCES tests/load_test.cpp)
add_executable(load_test ${LOAD_TEST_SOURCES})
target_link_libraries(load_test ${CONAN_LIBS})
add_test(NAME LoadTest COMMAND load_test --gtest_out xml:test_load.xml)
