
include_directories(${CMAKE_CURRENT_BINARY_DIR}/../proto)

set(UNIT_TEST_LIBS grpc_helper test_proto sisl
    ${CONAN_LIBS})

enable_testing()

add_executable(auth_test auth_test.cpp)
#add_dependencies(auth_test grpc_helper test_proto)
target_link_libraries(auth_test ${UNIT_TEST_LIBS} )
add_test(NAME Auth_Test COMMAND auth_test)



