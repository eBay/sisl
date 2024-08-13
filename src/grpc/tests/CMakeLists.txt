cmake_minimum_required (VERSION 3.11)

add_subdirectory(proto)

include_directories(BEFORE "${CMAKE_CURRENT_BINARY_DIR}/proto")

add_subdirectory(function)
add_subdirectory(unit)
