cmake_minimum_required (VERSION 3.11)

if (DEFINED ENABLE_TESTING)
  if(${ENABLE_TESTING})
    include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wisr_vector_test)
    target_sources(wisr_vector_test PRIVATE 
      tests/test_wisr_vector.cpp
      )
    target_link_libraries(wisr_vector_test sisl_metrics benchmark::benchmark GTest::gtest)
    add_test(NAME WisrVector COMMAND wisr_vector_test)

    add_executable(wisr_vector_benchmark)
    target_sources(wisr_vector_benchmark PRIVATE 
      tests/wisr_vector_benchmark.cpp
      )
    target_link_libraries(wisr_vector_benchmark sisl_metrics benchmark::benchmark)

    add_executable(wisr_list_benchmark)
    target_sources(wisr_list_benchmark PRIVATE 
      tests/wisr_list_benchmark.cpp
      )
    target_link_libraries(wisr_list_benchmark sisl_metrics benchmark::benchmark)

    add_executable(wisr_deque_benchmark)
    target_sources(wisr_deque_benchmark PRIVATE 
      tests/wisr_deque_benchmark.cpp
      )
    target_link_libraries(wisr_deque_benchmark sisl_metrics benchmark::benchmark)

    add_executable(wisr_intrusive_slist_benchmark)
    target_sources(wisr_intrusive_slist_benchmark PRIVATE 
      tests/wisr_intrusive_slist_benchmark.cpp
      )
    target_link_libraries(wisr_intrusive_slist_benchmark sisl_metrics benchmark::benchmark)
  endif()
endif()
