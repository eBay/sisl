cmake_minimum_required (VERSION 3.10)
project (async_http)

enable_testing()

if(EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
    include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
    conan_basic_setup()
else()
    message(WARNING "The file conanbuildinfo.cmake doesn't exist, you have to run conan install first")
    return()
endif()

if (NOT ${CONAN_SETTINGS_COMPILER} STREQUAL "clang" AND NOT ${CONAN_SETTINGS_COMPILER} STREQUAL "apple-clang")
    include (cmake/debug_flags.cmake)
endif()

if  (${MEMORY_SANITIZER_ON})
    include (cmake/mem_sanitizer.cmake)
endif()

include(cmake/settings_gen.cmake)
find_package (Threads REQUIRED)

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif ()

if(${CMAKE_BUILD_TYPE} STREQUAL Debug)
    message("Debug build")
    add_compile_options(-D_DEBUG=1 -O0 -DDEBUG_RCU -D_PRERELEASE=1)
else()
    message("Release build")
    add_compile_options(-DNDEBUG=1 -O3)
endif()

add_compile_options(-g)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fconcepts")

add_subdirectory (src/async_http)
add_subdirectory (src/wisr)
add_subdirectory (src/metrics)
add_subdirectory (src/fds)
add_subdirectory (src/settings)
add_subdirectory (src/utility)

add_library(sisl
        $<TARGET_OBJECTS:sisl_metrics>
        )
target_link_libraries(sisl ${COMMON_LIB_DEPS})