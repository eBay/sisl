cmake_minimum_required(VERSION 3.11)
project(test_package)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

find_package(sisl CONFIG REQUIRED)

add_executable(${PROJECT_NAME} test_package.cpp example_decl.cpp)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
target_link_libraries(${PROJECT_NAME} sisl::sisl)
