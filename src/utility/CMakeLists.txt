cmake_minimum_required (VERSION 3.11)

if (DEFINED ENABLE_TESTING)
  if (${ENABLE_TESTING})
    include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(test_atomic_counter)
    target_sources(test_atomic_counter PRIVATE
      tests/test_atomic_counter.cpp
      )
    target_link_libraries(test_atomic_counter sisl_core GTest::gtest)
    add_test(NAME AtomicCounter COMMAND test_atomic_counter)

    add_executable(test_thread_buffer)
    target_sources(test_thread_buffer PRIVATE
      tests/test_thread_buffer.cpp
      )
    target_link_libraries(test_thread_buffer sisl_core GTest::gtest)
    add_test(NAME ThreadBuffer COMMAND test_thread_buffer)

    add_executable(test_status_factory)
    target_sources(test_status_factory PRIVATE
      tests/test_status_factory.cpp
      )
    target_link_libraries(test_status_factory sisl_core benchmark::benchmark)
    add_test(NAME StatusFactory COMMAND test_status_factory)

    add_executable(test_enum)
    target_sources(test_enum PRIVATE
      tests/test_enum.cpp
      )
    target_link_libraries(test_enum sisl_core GTest::gtest)
    add_test(NAME Enum COMMAND test_enum)

    if (${prerelease_dummy_FOUND})
      add_executable(test_objlife)
      target_sources(test_objlife PRIVATE
        tests/test_objlife_counter.cpp
        )
      target_link_libraries(test_objlife sisl_core GTest::gtest)
      add_test(NAME ObjLife COMMAND test_objlife)
    endif ()
  endif()
endif()
