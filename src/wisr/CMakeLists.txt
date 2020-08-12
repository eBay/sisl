set(CMAKE_CXX_STANDARD 17)
set(CPP_WARNINGS "-Wall -Wextra -Werror -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPP_FLAGS} ${CPP_WARNINGS} -DPACKAGE_NAME=${CONAN_PACKAGE_NAME} -DPACKAGE_VERSION=${CONAN_PACKAGE_VERSION}")

find_package(Threads REQUIRED)
find_package(Boost REQUIRED)

include_directories(BEFORE ../fds)
include_directories(BEFORE ..)

set(WISR_VECTOR_TEST tests/test_wisr_vector.cpp)
add_executable(wisr_vector_test ${WISR_VECTOR_TEST})
target_link_libraries(wisr_vector_test ${Boost_LIBRARIES} ${CONAN_LIBS})
add_test(NAME WisrVectorTest COMMAND wisr_vector_test)

set(WISR_VECTOR_BENCHMARK tests/wisr_vector_benchmark.cpp)
add_executable(wisr_vector_benchmark ${WISR_VECTOR_BENCHMARK})
target_link_libraries(wisr_vector_benchmark ${Boost_LIBRARIES} ${CONAN_LIBS})

set(WISR_LIST_BENCHMARK tests/wisr_list_benchmark.cpp)
add_executable(wisr_list_benchmark ${WISR_LIST_BENCHMARK})
target_link_libraries(wisr_list_benchmark ${Boost_LIBRARIES} ${CONAN_LIBS})

set(WISR_DEQUE_BENCHMARK tests/wisr_deque_benchmark.cpp)
add_executable(wisr_deque_benchmark ${WISR_DEQUE_BENCHMARK})
target_link_libraries(wisr_deque_benchmark ${Boost_LIBRARIES} ${CONAN_LIBS})

set(WISR_INTRUSIVE_SLIST_BENCHMARK tests/wisr_intrusive_slist_benchmark.cpp)
add_executable(wisr_intrusive_slist_benchmark ${WISR_INTRUSIVE_SLIST_BENCHMARK})
target_link_libraries(wisr_intrusive_slist_benchmark ${Boost_LIBRARIES} ${CONAN_LIBS})
