#include <iostream>
#include "bitword.hpp"
#include <sds_logging/logging.h>
#include <sds_options/options.h>

using namespace sisl;

SDS_LOGGING_INIT(test_bitword);

static void validate(uint64_t val, int offset, const bit_filter& filter, const bit_match_result& exp_result) {
    Bitword< unsafe_bits< uint64_t > > bword(val);

    const auto result = bword.get_next_reset_bits_filtered(offset, filter);
    if (result.match_type != exp_result.match_type) {
        LOGERROR("Val={:064b}(0x{:x}) offset={} filter=[{}] Mismatch match_type expected=[{}] actual=[{}]: FAILED", val,
                 val, offset, filter.to_string(), exp_result.to_string(), result.to_string());
        return;
    }

    if (result.match_type != bit_match_type::no_match) {
        if ((result.start_bit != exp_result.start_bit) || (result.count != exp_result.count)) {
            LOGERROR("Val={:064b}(0x{:x}) offset={} filter=[{}] Mismatch bit/count expected=[{}] actual=[{}]: FAILED",
                     val, val, offset, filter.to_string(), exp_result.to_string(), result.to_string());
            return;
        }
    }
#if 0
    if (result.match_type == bit_match_type::no_match) {
        if ((result.next_best_lsb != exp_result.next_best_lsb) || (result.next_best_mid != exp_result.next_best_mid) ||
            (result.next_best_msb != exp_result.next_best_msb)) {
            LOGERROR(
                "Val={:064b}(0x{:x}) offset={} filter=[{}] Mismatch next_best_result expected=[{}] actual=[{}]: FAILED",
                val, val, offset, filter.to_string(), exp_result.to_string(), result.to_string());
            return;
        }
    } else if (result.matched != exp_result.matched) {
        LOGERROR("Val={:064b}(0x{:x}) offset={} filter=[{}] Mismatch bit/count expected=[{}] actual=[{}]: FAILED", val,
                 val, offset, filter.to_string(), exp_result.to_string(), result.to_string());
    }
#endif

    LOGINFO("Val={:064b}(0x{:x}) offset={} filter=[{}] result=[{}] : Passed", val, val, offset, filter.to_string(),
            result.to_string());
}

SDS_OPTIONS_ENABLE(logging)
int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging)
    sds_logging::SetLogger("test_bitword");
    spdlog::set_pattern("[%D %T%z] [%^%L%$] [%t] %v");

    validate(0xfff0, 0, {5, 5, 1}, {bit_match_type::msb_match, 16, 48});
    validate(0xfff0, 0, {4, 5, 1}, {bit_match_type::lsb_match, 0, 4});

    validate(0x0, 0, {5, 5, 1}, {bit_match_type::lsb_match, 0, 64});
    validate(0x0, 0, {64, 70, 1}, {bit_match_type::lsb_match, 0, 64});
    validate(0xffffffffffffffff, 0, {5, 5, 1}, {bit_match_type::no_match, 0, 0});

    validate(0x7fffffffffffffff, 0, {2, 2, 1}, {bit_match_type::msb_match, 63, 1});
    validate(0x7f0f0f000f0f0f0f, 0, {2, 2, 1}, {bit_match_type::mid_match, 4, 4});
    validate(0x7f0f0f000f0f0f0f, 29, {2, 2, 1}, {bit_match_type::mid_match, 29, 11});

    validate(0x8000000000000000, 0, {5, 8, 1}, {bit_match_type::lsb_match, 0, 63});
    validate(0x8000000000000001, 0, {5, 8, 1}, {bit_match_type::mid_match, 1, 62});
    validate(0x8000000000000001, 10, {8, 8, 1}, {bit_match_type::mid_match, 10, 53});

    validate(0x7fffffffffffffff, 0, {1, 1, 1}, {bit_match_type::msb_match, 63, 1});
    validate(0x7fffffffffffffff, 56, {1, 1, 1}, {bit_match_type::msb_match, 63, 1});
    validate(0x7fffffffffffffff, 56, {2, 2, 1}, {bit_match_type::msb_match, 63, 1});

    validate(0x7ff000ffff00ff0f, 0, {11, 11, 1}, {bit_match_type::mid_match, 40, 12});
    validate(0x7ff000ffff00ff0f, 5, {2, 2, 1}, {bit_match_type::mid_match, 5, 3});
    validate(0x7ff000ffff00ff0f, 5, {8, 8, 1}, {bit_match_type::mid_match, 16, 8});

    validate(0x8fffff0f0f0f00f4, 0, {3, 9, 1}, {bit_match_type::no_match, 0, 0});
    validate(0x8ff00f0f0f0f00f4, 1, {3, 9, 1}, {bit_match_type::no_match, 0, 0});
    validate(0x7ff00f0f0f0f00f4, 0, {3, 9, 2}, {bit_match_type::no_match, 0, 0});
    validate(0x00ff0f0f0f0ff0f4, 0, {3, 9, 9}, {bit_match_type::no_match, 0, 0});

#if 0
    validate(0x8fffff0f0f0f00f4, 0, {3, 9, 1}, {bit_match_type::no_match, {0, 0}, {0, 2}, {8, 8}, {0, 0}});
    validate(0x8ff00f0f0f0f00f4, 1, {3, 9, 1}, {bit_match_type::no_match, {0, 0}, {0, 0}, {8, 8}, {0, 0}});
    validate(0x7ff00f0f0f0f00f4, 0, {3, 9, 2}, {bit_match_type::no_match, {0, 0}, {0, 2}, {8, 8}, {63, 1}});
    validate(0x00ff0f0f0f0ff0f4, 0, {3, 9, 9}, {bit_match_type::no_match, {0, 0}, {0, 2}, {56, 8}, {56, 8}});
#endif
}

#if 0
int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging)
    sds_logging::SetLogger("test_bitword");
    spdlog::set_pattern("[%D %T%z] [%^%L%$] [%t] %v");

    validate(0xfff0, 0, {5, 5, 1}, {bit_match_type::mid_match, {16, 48}});
    validate(0xfff0, 0, {4, 5, 1}, {bit_match_type::lsb_match, {0, 4}});

    validate(0x0, 0, {5, 5, 1}, {bit_match_type::lsb_match, {0, 64}});
    validate(0x0, 0, {64, 70, 1}, {bit_match_type::lsb_match, {0, 64}});
    validate(0xffffffffffffffff, 0, {5, 5, 1}, {bit_match_type::no_match, {0, 0}});

    validate(0x7fffffffffffffff, 0, {2, 2, 1}, {bit_match_type::msb_match, {63, 1}});
    validate(0x7f0f0f0f0f0f0f0f, 0, {2, 2, 1}, {bit_match_type::mid_match, {4, 4}});

    validate(0x8000000000000000, 0, {5, 8, 1}, {bit_match_type::lsb_match, {0, 63}});
    validate(0x8000000000000001, 0, {5, 8, 1}, {bit_match_type::mid_match, {1, 62}});
    validate(0x8000000000000001, 10, {8, 8, 1}, {bit_match_type::mid_match, {10, 53}});

    validate(0x7fffffffffffffff, 0, {1, 1, 1}, {bit_match_type::mid_match, {63, 1}});
    validate(0x7fffffffffffffff, 56, {1, 1, 1}, {bit_match_type::mid_match, {63, 1}});
    validate(0x7fffffffffffffff, 56, {2, 2, 1}, {bit_match_type::msb_match, {63, 1}});

    validate(0x7ff000ffff00ff0f, 0, {11, 11, 1}, {bit_match_type::mid_match, {40, 12}});
    validate(0x7ff000ffff00ff0f, 5, {2, 2, 1}, {bit_match_type::mid_match, {5, 3}});
    validate(0x7ff000ffff00ff0f, 5, {8, 8, 1}, {bit_match_type::mid_match, {16, 8}});

    validate(0x8fffff0f0f0f00f4, 0, {3, 9, 1}, {bit_match_type::no_match, {0, 0}, {0, 2}, {8, 8}, {0, 0}});
    validate(0x8ff00f0f0f0f00f4, 1, {3, 9, 1}, {bit_match_type::no_match, {0, 0}, {0, 0}, {8, 8}, {0, 0}});
    validate(0x7ff00f0f0f0f00f4, 0, {3, 9, 2}, {bit_match_type::no_match, {0, 0}, {0, 2}, {8, 8}, {63, 1}});
    validate(0x00ff0f0f0f0ff0f4, 0, {3, 9, 9}, {bit_match_type::no_match, {0, 0}, {0, 2}, {56, 8}, {56, 8}});
}
#endif