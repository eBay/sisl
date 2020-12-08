set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror -Wpedantic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPP_WARNINGS} -DPACKAGE_NAME=${CONAN_PACKAGE_NAME} -DPACKAGE_VERSION=${CONAN_PACKAGE_VERSION}")

include_directories(BEFORE include)

file(GLOB API_HEADERS include/*.h)
file(GLOB LIB_HEADERS lib/*.h)
file(GLOB LIB_SOURCES lib/*.cpp)

add_library(sds_options
  ${API_HEADERS}
  ${LIB_HEADERS}
  ${LIB_SOURCES}
)

set(BASIC_TEST_SOURCES tests/basic.cpp)
add_executable(basic_test ${BASIC_TEST_SOURCES})
target_link_libraries(basic_test sds_options ${CONAN_LIBS})
set(extra_args "")
if (${CONAN_BUILD_COVERAGE})
set(extra_args "--gtest_output=xml:/output/test_basic.xml")
endif ()
add_test(NAME BasicTest COMMAND basic_test ${extra_args})
