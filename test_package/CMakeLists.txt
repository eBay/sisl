cmake_minimum_required(VERSION 3.11)
project(test_package)

set(CMAKE_CXX_STANDARD 20)

find_package(sisl QUIET REQUIRED)

add_executable(${PROJECT_NAME} test_package.cpp example_decl.cpp)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_link_libraries(${PROJECT_NAME} sisl::sisl)

# Test linking just sisl::settings
add_executable(${PROJECT_NAME}_log_only test_package.cpp example_decl.cpp)
target_compile_features(${PROJECT_NAME}_log_only PUBLIC cxx_std_20)
target_link_libraries(${PROJECT_NAME}_log_only sisl::settings)

add_executable(${PROJECT_NAME}_grpc grpc_test.cpp)
target_compile_features(${PROJECT_NAME}_grpc PUBLIC cxx_std_20)
target_link_libraries(${PROJECT_NAME}_grpc sisl::grpc)

# Test linking just sisl::grpc
add_executable(${PROJECT_NAME}_grpc_only grpc_test.cpp)
target_compile_features(${PROJECT_NAME}_grpc_only PUBLIC cxx_std_20)
target_link_libraries(${PROJECT_NAME}_grpc_only sisl::grpc)
