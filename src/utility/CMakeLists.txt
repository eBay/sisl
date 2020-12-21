add_flags("-Wno-unused-parameter")

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(TEST_THREAD_BUFFER
    tests/test_thread_buffer.cpp
   )
add_executable(test_thread_buffer ${TEST_THREAD_BUFFER})
target_link_libraries(test_thread_buffer ${Boost_LIBRARIES} ${CONAN_LIBS})
add_test(NAME ThreadBufferTest COMMAND test_thread_buffer)

set(TEST_STATUS_FACTORY
    tests/test_status_factory.cpp
   )
add_executable(test_status_factory ${TEST_STATUS_FACTORY})
target_link_libraries(test_status_factory ${CONAN_LIBS})
add_test(NAME StatusFactoryTest COMMAND test_status_factory)
