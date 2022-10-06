cmake_minimum_required (VERSION 3.10)
project (sisl)

option(DEBUG_CMAKE "Debug CMake messages option" OFF)

# Set Global CMake Options
set_property(GLOBAL PROPERTY USE_FOLDERS ON) # turn on folder hierarchies

include (cmake/Flags.cmake)

set(CMAKE_CXX_STANDARD 20)
enable_testing()

if(EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
    include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
    conan_basic_setup(TARGETS)
else ()
    message("The file conanbuildinfo.cmake doesn't exist, some properties will be unavailable")
endif ()

if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    include (cmake/debug_flags.cmake)
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

find_package(benchmark REQUIRED)
find_package(Boost REQUIRED)
find_package(cpr REQUIRED)
find_package(cxxopts REQUIRED)
find_package(folly REQUIRED)
find_package(GTest REQUIRED)
find_package(gperftools REQUIRED)
find_package(jwt-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(prerelease_dummy QUIET)
find_package(prometheus-cpp REQUIRED)
find_package(zmarok-semver REQUIRED)
find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)
find_package(userspace-rcu REQUIRED)

list (APPEND COMMON_DEPS
    Boost::headers
    cxxopts::cxxopts
    nlohmann_json::nlohmann_json
    prometheus-cpp::prometheus-cpp
    spdlog::spdlog
    userspace-rcu::userspace-rcu
  )
if (${prerelease_dummy_FOUND})
    list (APPEND COMMON_DEPS prerelease_dummy::prerelease_dummy)
endif ()

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
if (DEFINED CONAN_PACKAGE_VERSION)
  message("Package Version: [${CONAN_PACKAGE_VERSION}]")
  add_flags("-DPACKAGE_VERSION=\\\"${CONAN_PACKAGE_VERSION}\\\"")
else ()
  message("Unknown Package Version")
  add_flags("-DPACKAGE_VERSION=\\\"${CONAN_PACKAGE_VERSION}\\\"")
endif ()

if(UNIX)
    # enable proper memstream flags
    add_flags("-D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE")
endif()

include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR}/src/auth_manager)
include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR}/src/settings)

#add_subdirectory (src/btree)
add_subdirectory (src/cache)
add_subdirectory (src/logging)
add_subdirectory (src/options)
add_subdirectory (src/wisr)
add_subdirectory (src/metrics)
add_subdirectory (src/fds)
add_subdirectory (src/settings)
add_subdirectory (src/utility)
add_subdirectory (src/sisl_version)
add_subdirectory (src/auth_manager)
add_subdirectory (src/file_watcher)
add_subdirectory (src/flip)

add_library(sisl
            $<TARGET_OBJECTS:sisl_metrics>
            $<TARGET_OBJECTS:sisl_cache>
            $<TARGET_OBJECTS:sisl_logging>
            $<TARGET_OBJECTS:sisl_options>
            $<TARGET_OBJECTS:sisl_version>
            $<TARGET_OBJECTS:sisl_settings>
            $<TARGET_OBJECTS:sisl_buffer>
            $<TARGET_OBJECTS:sisl_auth_manager>
            $<TARGET_OBJECTS:sisl_trf_client>
            $<TARGET_OBJECTS:sisl_file_watcher>
           )

list(APPEND SISL_DEPS 
    Folly::Folly
)

if (DEFINED MALLOC_IMPL)
    if (${MALLOC_IMPL} STREQUAL "tcmalloc")
        list(APPEND SISL_DEPS gperftools::tcmalloc)
    endif()
endif()

target_link_libraries(sisl
    ${SISL_DEPS}
)

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
