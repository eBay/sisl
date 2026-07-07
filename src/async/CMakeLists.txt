cmake_minimum_required(VERSION 3.11)

# sisl_async is header-only; the tests below need stdexec, now provided by the conan package
# (sisl requires stdexec/25.09 -> the stdexec::stdexec target; see 3rd_party/stdexec). find_package
# is QUIET + gated so a non-conan build without stdexec just skips these tests rather than failing.
if(BUILD_TESTING)
    # disk_task tests have no stdexec dependency — compile whenever tests are built.
    add_executable(test_disk_task)
    target_sources(test_disk_task PRIVATE tests/test_disk_task.cpp)
    target_link_libraries(test_disk_task PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME DiskTask COMMAND test_disk_task)

    # value_awaitable is header-only and stdexec-free (the cross-thread bridge primitive
    # behind sisl/grpc's coroutine reply); compile its tests unconditionally too.
    add_executable(test_value_awaitable)
    target_sources(test_value_awaitable PRIVATE tests/test_value_awaitable.cpp)
    target_link_libraries(test_value_awaitable PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME ValueAwaitable COMMAND test_value_awaitable)

    # shared_awaitable is the multi-consumer (broadcast) sibling of value_awaitable (the folly::SharedPromise
    # replacement); also header-only and stdexec-free.
    add_executable(test_shared_awaitable)
    target_sources(test_shared_awaitable PRIVATE tests/test_shared_awaitable.cpp)
    target_link_libraries(test_shared_awaitable PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME SharedAwaitable COMMAND test_shared_awaitable)

    find_package(stdexec QUIET)
    if (TARGET stdexec::stdexec)
        add_executable(test_cqe_awaitable)
        target_sources(test_cqe_awaitable PRIVATE
            tests/test_cqe_awaitable.cpp
        )
        target_link_libraries(test_cqe_awaitable PRIVATE
            stdexec::stdexec
            GTest::gtest
            GTest::gtest_main
        )
        add_test(NAME CqeAwaitable COMMAND test_cqe_awaitable)

        add_executable(test_manual_scheduler)
        target_sources(test_manual_scheduler PRIVATE
            tests/test_manual_scheduler.cpp
        )
        target_link_libraries(test_manual_scheduler PRIVATE
            stdexec::stdexec
            GTest::gtest
            GTest::gtest_main
        )
        add_test(NAME ManualScheduler COMMAND test_manual_scheduler)
    endif()
endif()
