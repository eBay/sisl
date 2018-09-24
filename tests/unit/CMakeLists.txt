

add_library(gtestall gtest-all.cpp)



# enable_testing()

file(GLOB TEST_SRC_FILES **/*.cpp)


MESSAGE( STATUS "TEST_SRC_FILES = " ${TEST_SRC_FILES} )
MESSAGE( STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY = " ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} )


# from list of files we'll create tests  
foreach(_test_file ${TEST_SRC_FILES})
    get_filename_component(_test_name ${_test_file} NAME_WE)
    add_executable(${_test_name} ${_test_file})
    
    target_link_libraries (${_test_name} ${CONAN_LIBS_GTEST})
    add_test(NAME ${_test_name} COMMAND ${_test_name} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    set_tests_properties(${_test_name} PROPERTIES TIMEOUT 5)
endforeach()




