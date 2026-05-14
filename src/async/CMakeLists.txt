cmake_minimum_required(VERSION 3.11)

# sisl_async is header-only; the tests below need stdexec available in
# the sisl build. The wiring is up to the consumer (FetchContent in a
# feature branch today, a Conan recipe later); this CMakeLists is
# inert if stdexec hasn't been declared as a target yet.
#
if (TARGET stdexec)
    add_executable(test_cqe_awaitable)
    target_sources(test_cqe_awaitable PRIVATE
        tests/test_cqe_awaitable.cpp
    )
    target_link_libraries(test_cqe_awaitable PRIVATE
        stdexec
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME CqeAwaitable COMMAND test_cqe_awaitable)

    add_executable(test_manual_scheduler)
    target_sources(test_manual_scheduler PRIVATE
        tests/test_manual_scheduler.cpp
    )
    target_link_libraries(test_manual_scheduler PRIVATE
        stdexec
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME ManualScheduler COMMAND test_manual_scheduler)
endif()
