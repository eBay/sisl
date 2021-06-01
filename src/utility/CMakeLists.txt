add_flags("-Wno-unused-parameter")

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(TEST_ATOMIC_COUNTER_SOURCES
    tests/test_atomic_counter.cpp
   )
add_executable(test_atomic_counter ${TEST_ATOMIC_COUNTER_SOURCES})
target_link_libraries(test_atomic_counter sisl ${CONAN_LIBS})
add_test(NAME atomic_counter COMMAND test_atomic_counter)

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

set(TEST_ENUM
    tests/test_enum.cpp
   )
add_executable(test_enum  ${TEST_ENUM})
target_link_libraries(test_enum  ${CONAN_LIBS})
add_test(NAME EnumTest COMMAND test_enum)
