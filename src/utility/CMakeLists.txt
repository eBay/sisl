cmake_minimum_required (VERSION 3.10)

add_flags("-Wno-unused-parameter")

include_directories(BEFORE .)

set(TEST_ATOMIC_COUNTER_SOURCES
    tests/test_atomic_counter.cpp
   )
add_executable(test_atomic_counter ${TEST_ATOMIC_COUNTER_SOURCES})
target_link_libraries(test_atomic_counter sisl ${COMMON_DEPS} GTest::gtest)
add_test(NAME atomic_counter COMMAND test_atomic_counter)

set(TEST_THREAD_BUFFER
    tests/test_thread_buffer.cpp
   )
add_executable(test_thread_buffer ${TEST_THREAD_BUFFER})
target_link_libraries(test_thread_buffer ${COMMON_DEPS} GTest::gtest)
add_test(NAME ThreadBufferTest COMMAND test_thread_buffer)

set(TEST_STATUS_FACTORY
    tests/test_status_factory.cpp
   )
add_executable(test_status_factory ${TEST_STATUS_FACTORY})
target_link_libraries(test_status_factory ${COMMON_DEPS} benchmark::benchmark)
add_test(NAME StatusFactoryTest COMMAND test_status_factory)

set(TEST_ENUM
    tests/test_enum.cpp
   )
add_executable(test_enum ${TEST_ENUM})
target_link_libraries(test_enum ${COMMON_DEPS} GTest::gtest)
add_test(NAME EnumTest COMMAND test_enum)

if (${prerelease_dummy_FOUND})
  set(TEST_OBJLIFE
      tests/test_objlife_counter.cpp
     )
  add_executable(test_objlife ${TEST_OBJLIFE})
  target_link_libraries(test_objlife sisl ${COMMON_DEPS} GTest::gtest)
  add_test(NAME ObjLifeTest COMMAND test_objlife)
endif ()
