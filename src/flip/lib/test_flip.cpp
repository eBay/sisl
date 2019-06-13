//
// Created by Kadayam, Hari on 28/03/18.
//

#include "flip_spec.pb.h"
#include "flip.hpp"
#include <memory>
#include <string>

void create_ret_fspec(flip::FlipSpec *fspec) {
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

void run_and_validate_ret_flip(flip::Flip *flip) {
    std::string my_coll = "item_shipping";
    std::string unknown_coll = "unknown_collection";

    auto result = flip->get_test_flip<std::string>("ret_fspec", my_coll);
    assert(result);
    assert(result.get() == "Error simulated value");

    result = flip->get_test_flip<std::string>("ret_fspec", unknown_coll);
    assert(!result);

    result = flip->get_test_flip<std::string>("ret_fspec", my_coll);
    assert(result);
    assert(result.get() == "Error simulated value");

    result = flip->get_test_flip<std::string>("ret_fspec", my_coll);
    assert(!result); // Not more than 2
}

void create_check_fspec(flip::FlipSpec *fspec) {
    *(fspec->mutable_flip_name()) = "check_fspec";

    auto cond = fspec->mutable_conditions()->Add();
    *cond->mutable_name() = "cmd_type";
    cond->set_oper(flip::Operator::EQUAL);
    cond->mutable_value()->set_int_value(1);

    auto freq = fspec->mutable_flip_frequency();
    freq->set_count(2);
    freq->set_percent(100);
}

void run_and_validate_check_flip(flip::Flip *flip) {
    int valid_cmd = 1;
    int invalid_cmd = -1;

    assert(!flip->test_flip("check_fspec", invalid_cmd));
    assert(flip->test_flip("check_fspec", valid_cmd));
    assert(!flip->test_flip("check_fspec", invalid_cmd));
    assert(flip->test_flip("check_fspec", valid_cmd));
    assert(!flip->test_flip("check_fspec", valid_cmd)); // Not more than 2
}

void create_delay_fspec(flip::FlipSpec *fspec) {
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

void run_and_validate_delay_flip(flip::Flip *flip) {
    int valid_cmd = 2;
    int invalid_cmd = -1;
    std::shared_ptr< std::atomic<int> > closure_calls = std::make_shared<std::atomic<int>>(0);

    assert(flip->delay_flip("delay_fspec", [closure_calls]() {
        (*closure_calls)++;
    }, valid_cmd));

    assert(!flip->delay_flip("delay_fspec", [closure_calls]() {
        (*closure_calls)++;
    }, invalid_cmd));

    assert(flip->delay_flip("delay_fspec", [closure_calls]() {
        (*closure_calls)++;
    }, valid_cmd));

    assert(!flip->delay_flip("delay_fspec", [closure_calls]() {
        (*closure_calls)++;
    }, invalid_cmd));

    assert(!flip->delay_flip("delay_fspec", [closure_calls]() {
        (*closure_calls)++;
    }, valid_cmd));

    sleep(2);
    DCHECK_EQ((*closure_calls).load(), 2);
}

void create_delay_ret_fspec(flip::FlipSpec *fspec) {
    *(fspec->mutable_flip_name()) = "delay_ret_fspec";

    auto cond = fspec->mutable_conditions()->Add();
    *cond->mutable_name() = "cmd_type";
    cond->set_oper(flip::Operator::EQUAL);
    cond->mutable_value()->set_int_value(2);

    fspec->mutable_flip_action()->mutable_delay_returns()->set_delay_in_usec(100000);
    fspec->mutable_flip_action()->mutable_delay_returns()->mutable_retval()->set_string_value("Delayed error simulated value");

    auto freq = fspec->mutable_flip_frequency();
    freq->set_count(2);
    freq->set_percent(100);
}

void run_and_validate_delay_return_flip(flip::Flip *flip) {
    int valid_cmd = 2;
    int invalid_cmd = -1;
    std::shared_ptr< std::atomic<int> > closure_calls = std::make_shared<std::atomic<int>>(0);

    assert(flip->get_delay_flip<std::string>("delay_ret_fspec", [closure_calls](std::string error) {
        (*closure_calls)++;
        DCHECK_EQ(error, "Delayed error simulated value");
    }, valid_cmd));

    assert(!flip->get_delay_flip<std::string>("delay_ret_fspec", [closure_calls](std::string error) {
        assert(0);
        (*closure_calls)++;
    }, invalid_cmd));

    assert(flip->get_delay_flip<std::string>("delay_ret_fspec", [closure_calls](std::string error) {
        DCHECK_EQ(error, "Delayed error simulated value");
        (*closure_calls)++;
    }, valid_cmd));

    assert(!flip->get_delay_flip<std::string>("delay_ret_fspec", [closure_calls](std::string error) {
        assert(0);
        (*closure_calls)++;
    }, invalid_cmd));

    assert(!flip->get_delay_flip<std::string>("delay_ret_fspec", [closure_calls](std::string error) {
        DCHECK_EQ(error, "Delayed error simulated value");
        (*closure_calls)++;
        LOG(INFO) << "Called with error = " << error;
    }, valid_cmd));

    sleep(2);
    DCHECK_EQ((*closure_calls).load(), 2);
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

int main(int argc, char *argv[]) {
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