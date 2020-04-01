#include <iostream>
#include "bitword.hpp"
#include <sds_logging/logging.h>
#include <sds_options/options.h>

using namespace sisl;

SDS_LOGGING_INIT(test_bitword);

static void validate(uint64_t val, int offset, const bit_filter& filter, int exp_start, bit_match_type exp_match) {
    Bitword< unsafe_bits< uint64_t > > bword(val);

    auto result = bword.get_next_reset_bits_filtered(offset, filter);
    if (result.match_type != exp_match) {
        LOGINFO("Val={} offset={} filter[{}] Expected type={} but got {}, result[{}]: FAILED", val, offset,
                filter.to_string(), (uint8_t)exp_match, (uint8_t)result.match_type, result.to_string());
        return;
        // assert(0);
    }

    if ((result.match_type != bit_match_type::no_match) && (result.start_bit != exp_start)) {
        LOGINFO("Val={} offset={} filter[{}] Expected start bit={} but got {}, result[{}] : FAILED", val, offset,
                filter.to_string(), exp_start, result.start_bit, result.to_string());
        return;
        // assert(0);
    }

    LOGINFO("Val={} offset={} filter[{}] result[{}] : Passed", val, offset, filter.to_string(), result.to_string());
}

SDS_OPTIONS_ENABLE(logging)
int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging)
    sds_logging::SetLogger("test_bitword");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    validate(0xfff0, 0, {5, 5, 1}, 16, bit_match_type::mid_match);
    validate(0xfff0, 0, {4, 5, 1}, 0, bit_match_type::lsb_match);

    validate(0x0, 0, {5, 5, 1}, 0, bit_match_type::lsb_match);
    validate(0x0, 0, {64, 70, 1}, 0, bit_match_type::lsb_match);
    validate(0xffffffffffffffff, 0, {5, 5, 1}, 0, bit_match_type::no_match);

    validate(0x7fffffffffffffff, 0, {2, 2, 1}, 63, bit_match_type::msb_match);
    validate(0x7f0f0f0f0f0f0f0f, 0, {2, 2, 1}, 4, bit_match_type::mid_match);

    validate(0x8000000000000000, 0, {5, 8, 1}, 0, bit_match_type::lsb_match);
    validate(0x8000000000000001, 0, {5, 8, 1}, 1, bit_match_type::mid_match);
    validate(0x8000000000000001, 10, {8, 8, 1}, 10, bit_match_type::mid_match);

    validate(0x7fffffffffffffff, 0, {1, 1, 1}, 63, bit_match_type::mid_match);
    validate(0x7fffffffffffffff, 56, {1, 1, 1}, 63, bit_match_type::mid_match);
    validate(0x7fffffffffffffff, 56, {2, 2, 1}, 63, bit_match_type::msb_match);

    validate(0x7ff000ffff00ff0f, 0, {11, 11, 1}, 40, bit_match_type::mid_match);
    validate(0x7ff000ffff00ff0f, 5, {2, 2, 1}, 5, bit_match_type::mid_match);
    validate(0x7ff000ffff00ff0f, 5, {8, 8, 1}, 16, bit_match_type::mid_match);
}
