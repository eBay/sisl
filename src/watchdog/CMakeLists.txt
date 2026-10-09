cmake_minimum_required(VERSION 3.11)

add_library(sisl_watchdog)
target_sources(sisl_watchdog PRIVATE
    watchdog_registry.cpp
)


if(BUILD_TESTING)
    add_executable(test_watchdog_registry)
    target_sources(test_watchdog_registry PRIVATE
        tests/test_watchdog_registry.cpp
    )
    target_link_libraries(test_watchdog_registry sisl_watchdog GTest::gtest)
    add_test(NAME WatchdogRegistryTest COMMAND test_watchdog_registry)
endif()