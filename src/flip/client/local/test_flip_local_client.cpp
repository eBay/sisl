/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#include "proto/flip_spec.pb.h"
#include "sisl/flip/flip_client.hpp"
#include <memory>
#include <string>

#include <sisl/options/options.h>
#include <sisl/logging/logging.h>

using namespace flip;

SISL_OPTIONS_ENABLE(logging)

Flip g_flip;

void run_and_validate_noret_flip() {
    int valid_cmd = 1;
    int invalid_cmd = -1;

    RELEASE_ASSERT(!g_flip.test_flip("noret_flip", invalid_cmd), "notret_flip invalid cmd succeeeded - unexpected");
    RELEASE_ASSERT(g_flip.test_flip("noret_flip", valid_cmd), "notret_flip valid cmd failed - unexpected");
    RELEASE_ASSERT(!g_flip.test_flip("noret_flip", invalid_cmd), "notret_flip valid cmd succeeeded - unexpected");
    RELEASE_ASSERT(g_flip.test_flip("noret_flip", valid_cmd), "notret_flip valid cmd failed - unexpected");
    RELEASE_ASSERT(!g_flip.test_flip("noret_flip", valid_cmd),
                   "notret_flip valid cmd succeeeded - no more than 2 expected to succeed"); // Not more than 2
}

void run_and_validate_ret_flip() {
    std::string my_vol = "vol1";
    std::string valid_dev_name = "/dev/sda";
    std::string unknown_vol = "unknown_vol";
    std::string invalid_dev_name = "/boot/sda";

    auto result = g_flip.get_test_flip< std::string >("simval_flip", my_vol, valid_dev_name);
    RELEASE_ASSERT(result, "get_test_flip failed for valid conditions - unexpected");
    RELEASE_ASSERT_EQ(result.get(), "Simulated error value", "Incorrect flip returned");

    result = g_flip.get_test_flip< std::string >("simval_flip", unknown_vol, valid_dev_name);
    RELEASE_ASSERT(!result, "get_test_flip succeeded for incorrect conditions - unexpected");

    result = g_flip.get_test_flip< std::string >("simval_flip", my_vol, invalid_dev_name);
    RELEASE_ASSERT(!result, "get_test_flip succeeded for incorrect conditions  - unexpected");

    result = g_flip.get_test_flip< std::string >("simval_flip", my_vol, valid_dev_name);
    RELEASE_ASSERT(result, "get_test_flip failed for valid conditions - unexpected");
    RELEASE_ASSERT_EQ(result.get(), "Simulated error value", "Incorrect flip returned");

    result = g_flip.get_test_flip< std::string >("simval_flip", my_vol, valid_dev_name);
    RELEASE_ASSERT(!result, "get_test_flip freq set to 2, but 3rd time hit as well - unexpected"); // Not more than 2
}

void run_and_validate_delay_flip() {
    int valid_cmd = 1;
    long valid_size_bytes1 = 2047;
    long valid_size_bytes2 = 2048;
    int invalid_cmd = -1;
    long invalid_size_bytes = 4096;
    std::shared_ptr< std::atomic< int > > closure_calls = std::make_shared< std::atomic< int > >(0);

    RELEASE_ASSERT(g_flip.delay_flip(
                       "delay_flip", [closure_calls]() { (*closure_calls)++; }, valid_cmd, valid_size_bytes1),
                   "delay_flip failed for valid conditions - unexpected");
    RELEASE_ASSERT(!g_flip.delay_flip(
                       "delay_flip", [closure_calls]() { (*closure_calls)++; }, invalid_cmd, valid_size_bytes1),
                   "delay_flip succeeded for invalid conditions - unexpected");
    RELEASE_ASSERT(g_flip.delay_flip(
                       "delay_flip", [closure_calls]() { (*closure_calls)++; }, valid_cmd, valid_size_bytes2),
                   "delay_flip failed for valid conditions - unexpected");
    RELEASE_ASSERT(!g_flip.delay_flip(
                       "delay_flip", [closure_calls]() { (*closure_calls)++; }, valid_cmd, invalid_size_bytes),
                   "delay_flip succeeded for invalid conditions - unexpected");
    RELEASE_ASSERT(!g_flip.delay_flip(
                       "delay_flip", [closure_calls]() { (*closure_calls)++; }, valid_cmd, valid_size_bytes1),
                   "delay_flip hit more than the frequency set - unexpected");

    sleep(2);
    RELEASE_ASSERT_EQ((*closure_calls).load(), 2, "Not all delay flips hit are called back");
}

void run_and_validate_delay_return_flip() {
    double valid_double = 2.0;
    double invalid_double = 1.85;
    std::shared_ptr< std::atomic< int > > closure_calls = std::make_shared< std::atomic< int > >(0);

    RELEASE_ASSERT(g_flip.get_delay_flip< std::string >(
                       "delay_simval_flip",
                       [closure_calls](std::string error) {
                           (*closure_calls)++;
                           RELEASE_ASSERT_EQ(error, "Simulated delayed errval", "Invalid closure called");
                       },
                       valid_double),
                   "delay_flip failed for valid conditions - unexpected");

    RELEASE_ASSERT(!g_flip.get_delay_flip< std::string >(
                       "delay_simval_flip",
                       [closure_calls](std::string) {
                           RELEASE_ASSERT(false, "Invalid closure called");
                           (*closure_calls)++;
                       },
                       invalid_double),
                   "delay_flip succeeded for invalid conditions - unexpected");

    RELEASE_ASSERT(g_flip.get_delay_flip< std::string >(
                       "delay_simval_flip",
                       [closure_calls](std::string error) {
                           RELEASE_ASSERT_EQ(error, "Simulated delayed errval", "Invalid closure called");
                           (*closure_calls)++;
                       },
                       valid_double),
                   "delay_flip failed for valid conditions - unexpected");

    RELEASE_ASSERT(!g_flip.get_delay_flip< std::string >(
                       "delay_simval_flip",
                       [closure_calls](std::string) {
                           RELEASE_ASSERT(false, "Invalid closure called");
                           (*closure_calls)++;
                       },
                       invalid_double),
                   "delay_flip succeeded for invalid conditions - unexpected");

    RELEASE_ASSERT(!g_flip.get_delay_flip< std::string >(
                       "delay_simval_flip",
                       [closure_calls](std::string error) {
                           RELEASE_ASSERT_EQ(error, "Simulated delayed errval", "Invalid closure called");
                           (*closure_calls)++;
                           LOGINFO("Called with error = {}", error);
                       },
                       valid_double),
                   "delay_flip hit more than the frequency set - unexpected");

    sleep(2);
    RELEASE_ASSERT_EQ((*closure_calls).load(), 2, "Not all delay flips hit are called back");
}

int main(int argc, char* argv[]) {
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger(std::string(argv[0]));
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    FlipClient fclient(&g_flip);
    FlipFrequency freq;

    /* Inject a no return action flip */
    FlipCondition cond1;
    fclient.create_condition("cmd_type", flip::Operator::EQUAL, (int)1, &cond1);
    freq.set_count(2);
    freq.set_percent(100);
    fclient.inject_noreturn_flip("noret_flip", {cond1}, freq);

    /* Inject a invalid return action flip */
    FlipCondition cond2, cond6;
    fclient.create_condition< std::string >("vol_name", flip::Operator::EQUAL, "vol1", &cond2);
    fclient.create_condition< std::string >("dev_name", flip::Operator::REG_EX, "\\/dev\\/", &cond6);
    freq.set_count(2);
    freq.set_percent(100);
    fclient.inject_retval_flip< std::string >("simval_flip", {cond2, cond6}, freq, "Simulated error value");

    /* Inject a delay of 100ms action flip */
    FlipCondition cond3, cond4;
    fclient.create_condition("cmd_type", flip::Operator::EQUAL, (int)1, &cond3);
    fclient.create_condition("size_bytes", flip::Operator::LESS_THAN_OR_EQUAL, (long)2048, &cond4);
    freq.set_count(2);
    freq.set_percent(100);
    fclient.inject_delay_flip("delay_flip", {cond3, cond4}, freq, 100000);

    /* Inject a delay of 1second and return a value action flip */
    FlipCondition cond5;
    fclient.create_condition("double_val", flip::Operator::NOT_EQUAL, (double)1.85, &cond5);
    freq.set_count(2);
    freq.set_percent(100);
    fclient.inject_delay_and_retval_flip< std::string >("delay_simval_flip", {cond5}, freq, 1000000,
                                                        "Simulated delayed errval");

    /* Now execute the flip and validate that they are correct */
    run_and_validate_noret_flip();
    run_and_validate_ret_flip();
    run_and_validate_delay_flip();
    run_and_validate_delay_return_flip();

    return 0;
}
