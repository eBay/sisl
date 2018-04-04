//
// Created by Kadayam, Hari on 28/03/18.
//

#include "proto/flip_spec.pb.h"
#include "flip.hpp"

void create_flip_spec(flip::FlipSpec *fspec) {
    *(fspec->mutable_flip_name()) = "fail_cmd";

    // Create a new condition and add it to flip spec
    auto cond1 = fspec->mutable_conditions()->Add();
    *cond1->mutable_name() = "cmd_type";
    cond1->set_oper(flip::Operator::EQUAL);
    cond1->mutable_value()->set_int_value(1);

    auto cond2 = fspec->mutable_conditions()->Add();
    *cond2->mutable_name() = "coll_name";
    cond2->set_oper(flip::Operator::EQUAL);
    cond2->mutable_value()->set_string_value("item_shipping");

    fspec->mutable_returns()->set_string_value("Error simulated");

    auto freq = fspec->mutable_flip_frequency();
    freq->set_count(1);
    freq->set_percent(100);
}

int main(int argc, char *argv[]) {
    flip::FlipSpec fspec;
    create_flip_spec(&fspec);

    flip::Flip flip;
    flip.add(fspec);

    int my_cmd = 1;
    std::string my_coll = "item_shipping";
    auto result = flip.get_test_flip<std::string>("fail_cmd", my_cmd, my_coll);
    if (result) {
        std::cout << "flip returned " << result.get() << "\n";
    }
    result = flip.get_test_flip<std::string>("fail_cmd", my_cmd, my_coll);
    if (result) {
        std::cout << "flip returned " << result.get();
    }

    return 0;
}