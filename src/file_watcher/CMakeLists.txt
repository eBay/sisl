add_flags("-Wno-unused-parameter")

include_directories(BEFORE ..)
include_directories(BEFORE .)

set(FILE_WATCHER_SOURCE_FILES 
    file_watcher.cpp
   )
add_library(sisl_file_watcher OBJECT ${FILE_WATCHER_SOURCE_FILES})

set(TEST_FILE_WATCHER_SOURCES
    file_watcher_test.cpp
   )
add_executable(test_file_watcher ${TEST_FILE_WATCHER_SOURCES})
target_link_libraries(test_file_watcher sisl ${Boost_LIBRARIES} ${CONAN_LIBS})
add_test(NAME test_file_watcher COMMAND test_file_watcher)