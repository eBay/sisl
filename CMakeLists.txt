cmake_minimum_required (VERSION 3.11)
project (sisl)

option(DEBUG_CMAKE "Debug CMake messages option" OFF)

# Set Global CMake Options
set_property(GLOBAL PROPERTY USE_FOLDERS ON) # turn on folder hierarchies

include (cmake/Flags.cmake)

set(CMAKE_CXX_STANDARD 20)

if (NOT BUILD_TESTING STREQUAL OFF)
  set(ENABLE_TESTING ON)
  enable_testing()
  find_package(GTest QUIET REQUIRED)
endif()

if (DEFINED BUILD_COVERAGE)
  if (${BUILD_COVERAGE})
    include (cmake/CodeCoverage.cmake)
    APPEND_COVERAGE_COMPILER_FLAGS()
    SETUP_TARGET_FOR_COVERAGE_GCOVR_XML(NAME coverage EXECUTABLE ctest DEPENDENCIES )
  endif()
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    message(STATUS "Debug build")
    add_flags("-D_DEBUG")
endif()

if (DEFINED MALLOC_IMPL)
    if (${MALLOC_IMPL} STREQUAL "jemalloc")
        add_flags("-DUSE_JEMALLOC=1")
        # turn off memory sanitizer if using jemalloc otherwise folly will cause double definition errors
        if (DEFINED MEMORY_SANITIZER_ON) 
            unset(MEMORY_SANITIZER_ON CACHE)
        endif()
    elseif (${MALLOC_IMPL} STREQUAL "tcmalloc")
        add_flags("-DUSING_TCMALLOC=1")
    endif()
endif()

if (DEFINED MEMORY_SANITIZER_ON)
    if (${MEMORY_SANITIZER_ON}) 
        include (cmake/mem_sanitizer.cmake)
        message(STATUS "********* WARNING: Running with Memory Sanitizer ON *********")
    endif()
else ()
    message(STATUS "********* WARNING: Running with Memory Sanitizer OFF *********")
endif()

include(cmake/settings_gen.cmake)

if (NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Windows") 
    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
endif()

find_package(Benchmark QUIET REQUIRED)
find_package(Boost QUIET REQUIRED)
find_package(cxxopts QUIET REQUIRED)
if (${MALLOC_IMPL} STREQUAL "tcmalloc")
  find_package(gperftools QUIET REQUIRED)
endif()

if (${MALLOC_IMPL} STREQUAL "jemalloc")
  find_package(jemalloc QUIET REQUIRED)
endif()

find_package(folly)
find_package(Boost)
find_package(breakpad)
find_package(cxxopts)
find_package(flatbuffers)
find_package(gRPC)
find_package(nlohmann_json)
find_package(prometheus-cpp)
find_package(userspace-rcu)
find_package(spdlog)
find_package(zmarok-semver)
find_package(benchmark)

list (APPEND COMMON_DEPS
    Boost::headers
    cxxopts::cxxopts
    nlohmann_json::nlohmann_json
    prometheus-cpp::prometheus-cpp
    spdlog::spdlog
  )
if(${breakpad_FOUND})
    list (APPEND COMMON_DEPS breakpad::breakpad)
endif()
if(${userspace-rcu_FOUND})
    list (APPEND COMMON_DEPS userspace-rcu::userspace-rcu)
endif()

if (${prerelease_dummy_FOUND})
    list (APPEND COMMON_DEPS prerelease_dummy::prerelease_dummy)
endif ()

if (DEFINED MALLOC_IMPL)
    if (${MALLOC_IMPL} STREQUAL "tcmalloc")
        list(APPEND COMMON_DEPS gperftools::gperftools)
    endif()

    if (${MALLOC_IMPL} STREQUAL "jemalloc")
        list(APPEND COMMON_DEPS jemalloc::jemalloc)
    endif()
endif()

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif ()

if((${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU") OR (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")) 
    # add compiler options
    add_flags("-fconcepts" Languages "CXX")
    
    # add warning options
    add_flags("-Wall -Wextra -Werror -Wpedantic")
endif()

# add conan information 
add_flags("-DPACKAGE_NAME=sisl")
if (DEFINED PACKAGE_VERSION)
  message("Package Version: [${PACKAGE_VERSION}]")
  add_flags("-DPACKAGE_VERSION=\\\"${PACKAGE_VERSION}\\\"")
else ()
  message("Unknown Package Version")
  add_flags("-DPACKAGE_VERSION=\\\"unknown\\\"")
endif ()

if(UNIX)
    # enable proper memstream flags
    add_flags("-D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE")
endif()

include_directories(BEFORE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_subdirectory(src)

# build info
string(TOUPPER "${CMAKE_BUILD_TYPE}" UC_CMAKE_BUILD_TYPE)
message(STATUS "Build configuration:       ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler info:           ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION} ${CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN}")
message(STATUS "C++ compiler info:         ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} ${CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN}")
message(STATUS "C flags:                   ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${UC_CMAKE_BUILD_TYPE}}")
message(STATUS "C++ flags:                 ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${UC_CMAKE_BUILD_TYPE}}")
message(STATUS "Linker flags (executable): ${CMAKE_EXE_LINKER_FLAGS} ${CMAKE_EXE_LINKER_FLAGS_${UC_CMAKE_BUILD_TYPE}}")
message(STATUS "Linker flags (shared):     ${CMAKE_SHARED_LINKER_FLAGS} ${CMAKE_SHARED_LINKER_FLAGS_${UC_CMAKE_BUILD_TYPE}}")
message(STATUS "Linker flags (module):     ${CMAKE_MODULE_LINKER_FLAGS} ${CMAKE_MODULE_LINKER_FLAGS_${UC_CMAKE_BUILD_TYPE}}")
message(STATUS "Linker flags (static):     ${CMAKE_STATIC_LINKER_FLAGS} ${CMAKE_STATIC_LINKER_FLAGS_${UC_CMAKE_BUILD_TYPE}}")
