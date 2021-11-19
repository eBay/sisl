include_directories(BEFORE include)

file(GLOB API_HEADERS include/*.h)
file(GLOB LIB_HEADERS lib/*.h)
file(GLOB LIB_SOURCES lib/*.cpp)

include_directories(BEFORE ..)
include_directories(BEFORE .)

add_library(sisl_options
            ${API_HEADERS}
            ${LIB_HEADERS}
            ${LIB_SOURCES}
           )

set(BASIC_TEST_SOURCES tests/basic.cpp)
add_executable(basic_test ${BASIC_TEST_SOURCES})
target_link_libraries(basic_test sisl_options ${CONAN_LIBS})
set(extra_args "")
if (${CONAN_BUILD_COVERAGE})
    set(extra_args "--gtest_output=xml:/output/test_basic.xml")
endif ()
add_test(NAME BasicTest COMMAND basic_test ${extra_args})
