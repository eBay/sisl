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
#include "flip.hpp"
#include <memory>
#include <string>

#include <sisl/options/options.h>

SISL_LOGGING_INIT(flip)
SISL_OPTIONS_ENABLE(logging)

void create_ret_fspec(flip::FlipSpec* fspec) {
    *(fspec->mutable_flip_name()) = "ret_fspec";

    // Create a new condition and add it to flip spec
    auto cond = fspec->mutable_conditions()->Add();
    *cond->mutable_name() = "coll_name";
    cond->set_oper(flip::Operator::EQUAL);
    cond->mutable_value()->set_string_value("item_shipping");

    fspec->mutable_flip_action()->mutable_returns()->mutable_retval()->set_string_value("Error simulated value");

    auto freq = fspec->mutable_flip_frequency();
    freq->set_count(2);
    freq->set_percent(100);
}

void run_and_validate_ret_flip(flip::Flip* flip) {
    std::string my_coll = "item_shipping";
    std::string unknown_coll = "unknown_collection";

    auto result = flip->get_test_flip< std::string >("ret_fspec", my_coll);
    RELEASE_ASSERT(result, "get_test_flip failed for valid conditions - unexpected");
    RELEASE_ASSERT_EQ(result.get(), "Error simulated value", "Incorrect flip returned");

    result = flip->get_test_flip< std::string >("ret_fspec", unknown_coll);
    RELEASE_ASSERT(!result, "get_test_flip succeeded for incorrect conditions - unexpected");

    result = flip->get_test_flip< std::string >("ret_fspec", my_coll);
    RELEASE_ASSERT(result, "get_test_flip failed for valid conditions - unexpected");
    RELEASE_ASSERT_EQ(result.get(), "Error simulated value", "Incorrect flip returned");

    result = flip->get_test_flip< std::string >("ret_fspec", my_coll);
    RELEASE_ASSERT(!result, "get_test_flip freq set to 2, but 3rd time hit as well - unexpected"); // Not more than 2
}

void create_check_fspec(flip::FlipSpec* fspec) {
    *(fspec->mutable_flip_name()) = "check_fspec";

    auto cond = fspec->mutable_conditions()->Add();
    *cond->mutable_name() = "cmd_type";
    cond->set_oper(flip::Operator::EQUAL);
    cond->mutable_value()->set_int_value(1);

    auto freq = fspec->mutable_flip_frequency();
    freq->set_count(2);
    freq->set_percent(100);
}

void run_and_validate_check_flip(flip::Flip* flip) {
    int valid_cmd = 1;
    int invalid_cmd = -1;

    RELEASE_ASSERT(!flip->test_flip("check_fspec", invalid_cmd),
                   "test_flip succeeded for incorrect conditions - unexpected");
    RELEASE_ASSERT(flip->test_flip("check_fspec", valid_cmd), "test_flip failed for valid conditions - unexpected");
    RELEASE_ASSERT(!flip->test_flip("check_fspec", invalid_cmd),
                   "test_flip succeeded for incorrect conditions - unexpected");
    RELEASE_ASSERT(flip->test_flip("check_fspec", valid_cmd), "test_flip failed for valid conditions - unexpected");
    RELEASE_ASSERT(!flip->test_flip("check_fspec", valid_cmd),
                   "test_flip freq set to 2, but 3rd time hit as well - unexpected"); // Not more than 2
}

void create_delay_fspec(flip::FlipSpec* fspec) {
    *(fspec->mutable_flip_name()) = "delay_fspec";

    auto cond = fspec->mutable_conditions()->Add();
    *cond->mutable_name() = "cmd_type";
    cond->set_oper(flip::Operator::EQUAL);
    cond->mutable_value()->set_int_value(2);

    fspec->mutable_flip_action()->mutable_delays()->set_delay_in_usec(100000);
    auto freq = fspec->mutable_flip_frequency();
    freq->set_count(2);
    freq->set_percent(100);
}

void run_and_validate_delay_flip(flip::Flip* flip) {
    int valid_cmd = 2;
    int invalid_cmd = -1;
    std::shared_ptr< std::atomic< int > > closure_calls = std::make_shared< std::atomic< int > >(0);

    RELEASE_ASSERT(flip->delay_flip(
                       "delay_fspec", [closure_calls]() { (*closure_calls)++; }, valid_cmd),
                   "delay_flip failed for valid conditions - unexpected");

    RELEASE_ASSERT(!flip->delay_flip(
                       "delay_fspec", [closure_calls]() { (*closure_calls)++; }, invalid_cmd),
                   "delay_flip succeeded for invalid conditions - unexpected");

    RELEASE_ASSERT(flip->delay_flip(
                       "delay_fspec", [closure_calls]() { (*closure_calls)++; }, valid_cmd),
                   "delay_flip failed for valid conditions - unexpected");

    RELEASE_ASSERT(!flip->delay_flip(
                       "delay_fspec", [closure_calls]() { (*closure_calls)++; }, invalid_cmd),
                   "delay_flip succeeded for invalid conditions - unexpected");

    RELEASE_ASSERT(!flip->delay_flip(
                       "delay_fspec", [closure_calls]() { (*closure_calls)++; }, valid_cmd),
                   "delay_flip hit more than the frequency set - unexpected");

    sleep(2);
    RELEASE_ASSERT_EQ((*closure_calls).load(), 2, "Not all delay flips hit are called back");
}

void create_delay_ret_fspec(flip::FlipSpec* fspec) {
    *(fspec->mutable_flip_name()) = "delay_ret_fspec";

    auto cond = fspec->mutable_conditions()->Add();
    *cond->mutable_name() = "cmd_type";
    cond->set_oper(flip::Operator::EQUAL);
    cond->mutable_value()->set_int_value(2);

    fspec->mutable_flip_action()->mutable_delay_returns()->set_delay_in_usec(100000);
    fspec->mutable_flip_action()->mutable_delay_returns()->mutable_retval()->set_string_value(
        "Delayed error simulated value");

    auto freq = fspec->mutable_flip_frequency();
    freq->set_count(2);
    freq->set_percent(100);
}

void run_and_validate_delay_return_flip(flip::Flip* flip) {
    int valid_cmd = 2;
    int invalid_cmd = -1;
    std::shared_ptr< std::atomic< int > > closure_calls = std::make_shared< std::atomic< int > >(0);

    RELEASE_ASSERT(flip->get_delay_flip< std::string >(
                       "delay_ret_fspec",
                       [closure_calls](std::string error) {
                           (*closure_calls)++;
                           DEBUG_ASSERT_EQ(error, "Delayed error simulated value");
                       },
                       valid_cmd),
                   "delay_flip failed for valid conditions - unexpected");

    RELEASE_ASSERT(!flip->get_delay_flip< std::string >(
                       "delay_ret_fspec",
                       [closure_calls](std::string error) {
                           assert(0);
                           (*closure_calls)++;
                       },
                       invalid_cmd),
                   "delay_flip succeeded for invalid conditions - unexpected");

    RELEASE_ASSERT(flip->get_delay_flip< std::string >(
                       "delay_ret_fspec",
                       [closure_calls](std::string error) {
                           DEBUG_ASSERT_EQ(error, "Delayed error simulated value");
                           (*closure_calls)++;
                       },
                       valid_cmd),
                   "delay_flip failed for valid conditions - unexpected");

    RELEASE_ASSERT(!flip->get_delay_flip< std::string >(
                       "delay_ret_fspec",
                       [closure_calls](std::string error) {
                           assert(0);
                           (*closure_calls)++;
                       },
                       invalid_cmd),
                   "delay_flip succeeded for invalid conditions - unexpected");

    RELEASE_ASSERT(!flip->get_delay_flip< std::string >(
                       "delay_ret_fspec",
                       [closure_calls](std::string error) {
                           DEBUG_ASSERT_EQ(error, "Delayed error simulated value");
                           (*closure_calls)++;
                           LOGINFO("Called with error = {}", error);
                       },
                       valid_cmd),
                   "delay_flip hit more than the frequency set - unexpected");

    sleep(2);
    RELEASE_ASSERT_EQ((*closure_calls).load(), 2, "Not all delay flips hit are called back");
}

#if 0
void create_multi_cond_fspec(flip::FlipSpec *fspec) {
    *(fspec->mutable_flip_name()) = "multi_cond1_fspec";

    // Create a new condition and add it to flip spec
    auto cond1 = fspec->mutable_conditions()->Add();
    *cond1->mutable_name() = "cmd_type";
    cond1->set_oper(flip::Operator::EQUAL);
    cond1->mutable_value()->set_int_value(1);

    auto cond2 = fspec->mutable_conditions()->Add();
    *cond2->mutable_name() = "coll_name";
    cond2->set_oper(flip::Operator::EQUAL);
    cond2->mutable_value()->set_string_value("item_shipping");

    fspec->mutable_flip_action()->mutable_returns()->mutable_return_()->set_string_value("Error simulated value");

    auto freq = fspec->mutable_flip_frequency();
    freq->set_count(2);
    freq->set_percent(100);
}
#endif

int main(int argc, char* argv[]) {
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger(std::string(argv[0]));
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    flip::FlipSpec ret_fspec;
    create_ret_fspec(&ret_fspec);

    flip::FlipSpec check_fspec;
    create_check_fspec(&check_fspec);

    flip::FlipSpec delay_fspec;
    create_delay_fspec(&delay_fspec);

    flip::FlipSpec delay_ret_fspec;
    create_delay_ret_fspec(&delay_ret_fspec);

    flip::Flip flip;
    flip.start_rpc_server();
    flip.add(ret_fspec);
    flip.add(check_fspec);
    flip.add(delay_fspec);
    flip.add(delay_ret_fspec);

    run_and_validate_ret_flip(&flip);
    run_and_validate_check_flip(&flip);
    run_and_validate_delay_flip(&flip);
    run_and_validate_delay_return_flip(&flip);

    return 0;
}
