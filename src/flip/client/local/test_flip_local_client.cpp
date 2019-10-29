//
// Created by Kadayam, Hari on 28/03/18.
//

#include "flip_spec.pb.h"
#include "flip.hpp"
#include <memory>
#include <string>

#include <sds_options/options.h>

using namespace flip;

SDS_LOGGING_INIT(flip)
SDS_OPTIONS_ENABLE(logging)

Flip g_flip;

void run_and_validate_noret_flip() {
    int valid_cmd = 1;
    int invalid_cmd = -1;

    assert(!g_flip.test_flip("noret_flip", invalid_cmd));
    assert(g_flip.test_flip("noret_flip", valid_cmd));
    assert(!g_flip.test_flip("noret_flip", invalid_cmd));
    assert(g_flip.test_flip("noret_flip", valid_cmd));
    assert(!g_flip.test_flip("noret_flip", valid_cmd)); // Not more than 2
}

void run_and_validate_ret_flip() {
    std::string my_vol = "vol1";
    std::string valid_dev_name = "/dev/sda";
    std::string unknown_vol = "unknown_vol";
    std::string invalid_dev_name = "/boot/sda";

    auto result = g_flip.get_test_flip<std::string>("simval_flip", my_vol, valid_dev_name);
    assert(result);
    assert(result.get() == "Simulated error value");

    result = g_flip.get_test_flip<std::string>("simval_flip", unknown_vol, valid_dev_name);
    assert(!result);

    result = g_flip.get_test_flip<std::string>("simval_flip", my_vol, invalid_dev_name);
    assert(!result);

    result = g_flip.get_test_flip<std::string>("simval_flip", my_vol, valid_dev_name);
    assert(result);
    assert(result.get() == "Simulated error value");

    result = g_flip.get_test_flip<std::string>("simval_flip", my_vol, valid_dev_name);
    assert(!result); // Not more than 2
}

void run_and_validate_delay_flip() {
    int valid_cmd = 1;
    long valid_size_bytes1 = 2047;
    long valid_size_bytes2 = 2048;
    int invalid_cmd = -1;
    long invalid_size_bytes = 4096;
    std::shared_ptr< std::atomic<int> > closure_calls = std::make_shared<std::atomic<int>>(0);

    assert(g_flip.delay_flip("delay_flip", [closure_calls]() {(*closure_calls)++;}, valid_cmd, valid_size_bytes1));
    assert(!g_flip.delay_flip("delay_flip", [closure_calls]() {(*closure_calls)++;}, invalid_cmd, valid_size_bytes1));
    assert(g_flip.delay_flip("delay_flip", [closure_calls]() { (*closure_calls)++;}, valid_cmd, valid_size_bytes2));
    assert(!g_flip.delay_flip("delay_flip", [closure_calls]() {(*closure_calls)++;}, valid_cmd, invalid_size_bytes));
    assert(!g_flip.delay_flip("delay_flip", [closure_calls]() {(*closure_calls)++;}, valid_cmd, valid_size_bytes1));

    sleep(2);
    DEBUG_ASSERT_EQ((*closure_calls).load(), 2);
}

void run_and_validate_delay_return_flip() {
    double valid_double = 2.0;
    double invalid_double = 1.85;
    std::shared_ptr< std::atomic<int> > closure_calls = std::make_shared<std::atomic<int>>(0);

    assert(g_flip.get_delay_flip<std::string>("delay_simval_flip", [closure_calls](std::string error) {
        (*closure_calls)++;
        DEBUG_ASSERT_EQ(error, "Simulated delayed errval");
    }, valid_double));

    assert(!g_flip.get_delay_flip<std::string>("delay_simval_flip", [closure_calls](std::string error) {
        assert(0);
        (*closure_calls)++;
    }, invalid_double));

    assert(g_flip.get_delay_flip<std::string>("delay_simval_flip", [closure_calls](std::string error) {
        DEBUG_ASSERT_EQ(error, "Simulated delayed errval");
        (*closure_calls)++;
    }, valid_double));

    assert(!g_flip.get_delay_flip<std::string>("delay_simval_flip", [closure_calls](std::string error) {
        assert(0);
        (*closure_calls)++;
    }, invalid_double));

    assert(!g_flip.get_delay_flip<std::string>("delay_simval_flip", [closure_calls](std::string error) {
        DEBUG_ASSERT_EQ(error, "Simulated delayed errval");
        (*closure_calls)++;
        LOGINFO("Called with error = {}", error);
    }, valid_double));

    sleep(2);
    DEBUG_ASSERT_EQ((*closure_calls).load(), 2);
}

int main(int argc, char *argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging)
    sds_logging::SetLogger(std::string(argv[0]));
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    FlipClient fclient(&g_flip);
    FlipFrequency freq;

    /* Inject a no return action flip */
    FlipCondition cond1;
    fclient.create_condition("cmd_type", flip::Operator::EQUAL, (int)1, &cond1);
    freq.set_count(2); freq.set_percent(100);
    fclient.inject_noreturn_flip("noret_flip", {cond1}, freq);

    /* Inject a invalid return action flip */
    FlipCondition cond2, cond6;
    fclient.create_condition<std::string>("vol_name", flip::Operator::EQUAL, "vol1", &cond2);
    fclient.create_condition<std::string>("dev_name", flip::Operator::REG_EX, "\\/dev\\/", &cond6);
    freq.set_count(2); freq.set_percent(100);
    fclient.inject_retval_flip<std::string>("simval_flip", {cond2, cond6}, freq, "Simulated error value");

    /* Inject a delay of 100ms action flip */
    FlipCondition cond3, cond4;
    fclient.create_condition("cmd_type", flip::Operator::EQUAL, (int)1, &cond3);
    fclient.create_condition("size_bytes", flip::Operator::LESS_THAN_OR_EQUAL, (long)2048, &cond4);
    freq.set_count(2); freq.set_percent(100);
    fclient.inject_delay_flip("delay_flip", {cond3, cond4}, freq, 100000);

    /* Inject a delay of 1second and return a value action flip */
    FlipCondition cond5;
    fclient.create_condition("double_val", flip::Operator::NOT_EQUAL, (double)1.85, &cond5);
    freq.set_count(2); freq.set_percent(100);
    fclient.inject_delay_and_retval_flip<std::string>("delay_simval_flip", {cond5}, freq, 1000000, "Simulated delayed errval");

    /* Now execute the flip and validate that they are correct */
    run_and_validate_noret_flip();
    run_and_validate_ret_flip();
    run_and_validate_delay_flip();
    run_and_validate_delay_return_flip();

    return 0;
}
