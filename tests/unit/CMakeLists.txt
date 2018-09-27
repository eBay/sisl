

add_library(gtestall gtest-all.cpp)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../proto)

# enable_testing()

file(GLOB TEST_SRC_FILES **/*.cpp)


MESSAGE( STATUS "TEST_SRC_FILES = " ${TEST_SRC_FILES} )
MESSAGE( STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY = " ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} )


# from list of files we'll create tests  
foreach(_test_file ${TEST_SRC_FILES})
    get_filename_component(_test_name ${_test_file} NAME_WE)
    add_executable(${_test_name} ${_test_file})
    
    add_dependencies(${_test_name} sds_grpc )
    target_link_libraries (${_test_name} sds_grpc gtestall ${CONAN_LIBS} )
    add_test(NAME ${_test_name} COMMAND ${_test_name} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    set_tests_properties(${_test_name} PROPERTIES TIMEOUT 5)
endforeach()




