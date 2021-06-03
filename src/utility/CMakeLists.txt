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

set(TEST_ENUM
    tests/test_enum.cpp
   )
add_executable(test_enum ${TEST_ENUM})
target_link_libraries(test_enum  ${CONAN_LIBS})
add_test(NAME EnumTest COMMAND test_enum)

set(TEST_OBJLIFE
    tests/test_objlife_counter.cpp
   )
add_executable(test_objlife ${TEST_OBJLIFE})
target_link_libraries(test_objlife sisl ${CONAN_LIBS})
add_test(NAME ObjLifeTest COMMAND test_objlife)