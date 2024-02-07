cmake_minimum_required(VERSION 3.11)
project(test_package)

set(CMAKE_CXX_STANDARD 20)

find_package(sisl QUIET REQUIRED)

add_executable(${PROJECT_NAME} test_package.cpp example_decl.cpp)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_link_libraries(${PROJECT_NAME} sisl::settings)
