cmake_minimum_required(VERSION 3.11)

if(${MALLOC_IMPL} STREQUAL "tcmalloc")
  find_package(gperftools REQUIRED)
  list(APPEND CORE_DEPS gperftools::gperftools)
endif()

if(${MALLOC_IMPL} STREQUAL "jemalloc")
  find_package(jemalloc REQUIRED)
  list(APPEND CORE_DEPS jemalloc::jemalloc)
endif()

find_package(folly REQUIRED)
find_package(Boost REQUIRED)
find_package(cxxopts REQUIRED)
find_package(flatbuffers REQUIRED)
find_package(gRPC QUIET REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(prometheus-cpp REQUIRED)
find_package(userspace-rcu REQUIRED)
find_package(spdlog REQUIRED)
find_package(zmarok-semver REQUIRED)

find_package(benchmark)
find_package(breakpad)

# Self contained libraries which ::core depends on
add_subdirectory(logging)
add_subdirectory(options)
add_subdirectory(settings)
add_subdirectory(version)

list(APPEND CORE_DEPS
  sisl_version
  sisl_logging
  sisl_options
  prometheus-cpp::prometheus-cpp
  userspace-rcu::userspace-rcu
  )

if(${breakpad_FOUND})
  list(APPEND CORE_DEPS breakpad::breakpad)
endif()

# SISL::core
add_subdirectory(fds)
add_subdirectory(metrics)
add_subdirectory(utility)
add_subdirectory(wisr)

list(APPEND CORE_LIBS
  $<TARGET_OBJECTS:sisl_buffer>
  $<TARGET_OBJECTS:sisl_metrics>
  )
list(APPEND CORE_DEPS
  folly::folly
  breakpad::breakpad
  )

add_library(sisl_core ${CORE_LIBS})
target_link_libraries(sisl_core PUBLIC sisl_metrics ${CORE_DEPS})

# Extensions
add_subdirectory(cache)
add_subdirectory(file_watcher)
add_subdirectory(flip)
add_subdirectory(grpc)
add_subdirectory(sobject)
