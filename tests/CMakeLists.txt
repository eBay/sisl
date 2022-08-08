cmake_minimum_required (VERSION 3.11)

find_package(GTest CONFIG REQUIRED)

add_subdirectory(proto)

enable_testing()

include_directories(BEFORE "${CMAKE_CURRENT_BINARY_DIR}/proto")

add_subdirectory(function)
add_subdirectory(unit)
