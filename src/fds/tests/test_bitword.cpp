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
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include <gtest/gtest.h>

#include "sisl/fds/bitword.hpp"

using namespace sisl;

SISL_LOGGING_INIT(test_bitword)
SISL_OPTIONS_ENABLE(logging)

namespace {
bool validate(const uint64_t val, const uint8_t offset, bit_filter filter, const uint8_t exp_start,
              const bit_match_type exp_match, const uint8_t exp_count) {
    Bitword< unsafe_bits< uint64_t > > bword(val);

    const auto result{bword.get_next_reset_bits_filtered(offset, filter)};
    if (result.match_type != exp_match) {
        LOGINFO("Val={} offset={} filter[{}] Expected type={} but got {}, result[{}]: FAILED", val,
                static_cast< uint16_t >(offset), filter.to_string(), static_cast< uint16_t >(exp_match),
                static_cast< uint16_t >(result.match_type), result.to_string());
        return false;
    }

    if ((result.match_type != bit_match_type::no_match) &&
        ((result.start_bit != exp_start) || (result.count != exp_count))) {
        LOGINFO("Val={} offset={} filter[{}] Expected start bit={} & count={} but got {} & {}, result[{}] : FAILED",
                val, static_cast< uint16_t >(offset), filter.to_string(), static_cast< uint16_t >(exp_start),
                static_cast< uint16_t >(exp_count), static_cast< uint16_t >(result.start_bit),
                static_cast< uint16_t >(result.count), result.to_string());
        return false;
    }
    return true;
}

class BitwordTest : public testing::Test {
public:
    BitwordTest() : testing::Test(){};
    BitwordTest(const BitwordTest&) = delete;
    BitwordTest(BitwordTest&&) noexcept = delete;
    BitwordTest& operator=(const BitwordTest&) = delete;
    BitwordTest& operator=(BitwordTest&&) noexcept = delete;
    virtual ~BitwordTest() override = default;

protected:
    void SetUp() override {}
    void TearDown() override {}
};

template < typename DataType >
void testLog2Base() {
    static_assert(std::is_unsigned_v< DataType >, "DataType must be unsigned.");
    ASSERT_EQ(logBase2(static_cast< DataType >(0)), static_cast< uint8_t >(255));
    DataType v{1};
    for (uint8_t bit{0}; bit < std::numeric_limits< DataType >::digits; ++bit, v <<= 1) {
        ASSERT_EQ(logBase2(v), bit);
    }
}
} // namespace

TEST_F(BitwordTest, TestLog2Base) {
    for (uint8_t x{0}; x < 255; ++x) {
        const uint8_t val{static_cast< uint8_t >(x + 1)};
        ASSERT_EQ(logBase2(val),
                  static_cast< uint8_t >(std::trunc(std::log2(val) + val * std::numeric_limits< double >::epsilon())));
    }

    testLog2Base< uint8_t >();
    testLog2Base< uint16_t >();
    testLog2Base< uint32_t >();
    testLog2Base< uint64_t >();
}

TEST_F(BitwordTest, TestSetCount) {
    const Bitword< unsafe_bits< uint64_t > > word1{0x1};
    ASSERT_EQ(word1.get_set_count(), 1);

    const Bitword< unsafe_bits< uint64_t > > word2{0x0};
    ASSERT_EQ(word2.get_set_count(), 0);

    const Bitword< unsafe_bits< uint64_t > > word3{0x100000000};
    ASSERT_EQ(word3.get_set_count(), 1);

    const Bitword< unsafe_bits< uint64_t > > word4{0xFFFFFFFFFFFFFFFF};
    ASSERT_EQ(word4.get_set_count(), 64);
}

TEST_F(BitwordTest, TestTrailingZeros) {
    ASSERT_EQ(get_trailing_zeros(0x01), static_cast< uint8_t >(0));
    ASSERT_EQ(get_trailing_zeros(0x02), static_cast< uint8_t >(1));
    ASSERT_EQ(get_trailing_zeros(static_cast< uint64_t >(0x00)), static_cast< uint8_t >(64));
#if __cplusplus > 201703L
    ASSERT_EQ(get_trailing_zeros(static_cast< uint32_t >(0x00)), static_cast< uint8_t >(32));
#endif
    ASSERT_EQ(get_trailing_zeros(0xf000000000), static_cast< uint8_t >(36));
    ASSERT_EQ(get_trailing_zeros(0xf00f000000000), static_cast< uint8_t >(36));
    ASSERT_EQ(get_trailing_zeros(0x8000000000000000), static_cast< uint8_t >(63));
}

TEST_F(BitwordTest, TestLeadingZeros) {
    ASSERT_EQ(get_leading_zeros(static_cast< uint64_t >(0x01)), static_cast< uint8_t >(63));
    ASSERT_EQ(get_leading_zeros(static_cast< uint64_t >(0x00)), static_cast< uint8_t >(64));
#if __cplusplus > 201703L
    ASSERT_EQ(get_leading_zeros(static_cast< uint32_t >(0x01)), static_cast< uint8_t >(31));
    ASSERT_EQ(get_leading_zeros(static_cast< uint32_t >(0x00)), static_cast< uint8_t >(32));
#endif
    ASSERT_EQ(get_leading_zeros(0xFFFFFFFFFFFFFFFF), static_cast< uint8_t >(0));
    ASSERT_EQ(get_leading_zeros(0x7FFFFFFFFFFFFFFF), static_cast< uint8_t >(1));
    ASSERT_EQ(get_leading_zeros(0x0FFFFFFFFFFFFFFF), static_cast< uint8_t >(4));
    ASSERT_EQ(get_leading_zeros(0x00FFFFFFFFFFFFFF), static_cast< uint8_t >(8));
    ASSERT_EQ(get_leading_zeros(0x00F0FFFFFFFFFFFF), static_cast< uint8_t >(8));
}

TEST_F(BitwordTest, TestResetCount) {
    const Bitword< unsafe_bits< uint64_t > > word1{0x1};
    ASSERT_EQ(word1.get_reset_count(), 63);

    const Bitword< unsafe_bits< uint64_t > > word2{0x0};
    ASSERT_EQ(word2.get_reset_count(), 64);

    const Bitword< unsafe_bits< uint64_t > > word3{0x100000000};
    ASSERT_EQ(word3.get_reset_count(), 63);

    const Bitword< unsafe_bits< uint64_t > > word4{0xFFFFFFFFFFFFFFFF};
    ASSERT_EQ(word4.get_reset_count(), 0);
}

TEST_F(BitwordTest, SetResetBit) {
    Bitword< unsafe_bits< uint64_t > > word1{0x0};
    ASSERT_EQ(word1.set_reset_bit(0, true), static_cast< uint64_t >(0x01));
    ASSERT_EQ(word1.set_reset_bit(63, true), static_cast< uint64_t >(0x8000000000000001));

    ASSERT_EQ(word1.set_reset_bit(0, false), static_cast< uint64_t >(0x8000000000000000));
    ASSERT_EQ(word1.set_reset_bit(63, false), static_cast< uint64_t >(0x00));
}

TEST_F(BitwordTest, SetBits) {
    Bitword< unsafe_bits< uint64_t > > word1{0x0};
    ASSERT_EQ(word1.set_bits(0, 2), static_cast< uint64_t >(0x03));
    ASSERT_EQ(word1.set_bits(62, 2), static_cast< uint64_t >(0xC000000000000003));
}

TEST_F(BitwordTest, ResetBits) {
    Bitword< unsafe_bits< uint64_t > > word1{0xFFFFFFFFFFFFFFFF};
    ASSERT_EQ(word1.reset_bits(0, 2), static_cast< uint64_t >(0xFFFFFFFFFFFFFFFC));
    ASSERT_EQ(word1.reset_bits(62, 2), static_cast< uint64_t >(0x3FFFFFFFFFFFFFFC));
}

TEST_F(BitwordTest, GetBitVal) {
    const Bitword< unsafe_bits< uint64_t > > word1{0x8000000000000001};
    ASSERT_TRUE(word1.get_bitval(63));
    ASSERT_TRUE(word1.get_bitval(0));
    ASSERT_FALSE(word1.get_bitval(62));
    ASSERT_FALSE(word1.get_bitval(1));
}

TEST_F(BitwordTest, IsBitSetReset) {
    const Bitword< unsafe_bits< uint64_t > > word1{0x8000000000000001};
    ASSERT_TRUE(word1.is_bit_set_reset(63, true));
    ASSERT_TRUE(word1.is_bit_set_reset(0, true));
    ASSERT_TRUE(word1.is_bit_set_reset(62, false));
    ASSERT_TRUE(word1.is_bit_set_reset(1, false));
}

TEST_F(BitwordTest, IsBitsSetReset) {
    const Bitword< unsafe_bits< uint64_t > > word1{0xC000000000000003};
    ASSERT_TRUE(word1.is_bits_set_reset(62, 2, true));
    ASSERT_TRUE(word1.is_bits_set_reset(0, 2, true));
    ASSERT_TRUE(word1.is_bits_set_reset(60, 2, false));
    ASSERT_TRUE(word1.is_bits_set_reset(2, 2, false));
}

TEST_F(BitwordTest, GetNextSetBit) {
    uint8_t bit_pos;
    const Bitword< unsafe_bits< uint64_t > > word1{0x05};
    ASSERT_TRUE(word1.get_next_set_bit(0, &bit_pos));
    ASSERT_EQ(bit_pos, static_cast< uint8_t >(0));
    ASSERT_TRUE(word1.get_next_set_bit(1, &bit_pos));
    ASSERT_EQ(bit_pos, static_cast< uint8_t >(2));

    const Bitword< unsafe_bits< uint64_t > > word2{0x8000000000000000};
    ASSERT_TRUE(word2.get_next_set_bit(0, &bit_pos));
    ASSERT_EQ(bit_pos, static_cast< uint8_t >(63));
    ASSERT_TRUE(word2.get_next_set_bit(8, &bit_pos));
    ASSERT_EQ(bit_pos, static_cast< uint8_t >(63));

    const Bitword< unsafe_bits< uint64_t > > word3{0x0};
    ASSERT_FALSE(word3.get_next_set_bit(0, &bit_pos));
    ASSERT_FALSE(word3.get_next_set_bit(8, &bit_pos));
}

TEST_F(BitwordTest, GetNextResetBit) {
    uint8_t bit_pos;
    const Bitword< unsafe_bits< uint64_t > > word1{0x02};
    ASSERT_TRUE(word1.get_next_reset_bit(0, &bit_pos));
    ASSERT_EQ(bit_pos, static_cast< uint8_t >(0));
    ASSERT_TRUE(word1.get_next_reset_bit(1, &bit_pos));
    ASSERT_EQ(bit_pos, static_cast< uint8_t >(2));

    const Bitword< unsafe_bits< uint64_t > > word2{0x7FFFFFFFFFFFFFFF};
    ASSERT_TRUE(word2.get_next_reset_bit(0, &bit_pos));
    ASSERT_EQ(bit_pos, static_cast< uint8_t >(63));
    ASSERT_TRUE(word2.get_next_reset_bit(8, &bit_pos));
    ASSERT_EQ(bit_pos, static_cast< uint8_t >(63));

    const Bitword< unsafe_bits< uint64_t > > word3{0xFFFFFFFFFFFFFFFF};
    ASSERT_FALSE(word3.get_next_reset_bit(0, &bit_pos));
    ASSERT_FALSE(word3.get_next_reset_bit(8, &bit_pos));
}

TEST_F(BitwordTest, GetNextResetBits) {
    uint8_t pcount;
    const Bitword< unsafe_bits< uint64_t > > word1{0x00};
    ASSERT_EQ(word1.get_next_reset_bits(0, &pcount), static_cast< uint8_t >(0));
    ASSERT_EQ(pcount, static_cast< uint8_t >(64));
    ASSERT_EQ(word1.get_next_reset_bits(8, &pcount), static_cast< uint8_t >(8));
    ASSERT_EQ(pcount, static_cast< uint8_t >(56));

    const Bitword< unsafe_bits< uint64_t > > word2{0xFFFFFFFFFFFFFF00};
    ASSERT_EQ(word2.get_next_reset_bits(0, &pcount), static_cast< uint8_t >(0));
    ASSERT_EQ(pcount, static_cast< uint8_t >(8));
    ASSERT_EQ(word2.get_next_reset_bits(4, &pcount), static_cast< uint8_t >(4));
    ASSERT_EQ(pcount, static_cast< uint8_t >(4));
    ASSERT_EQ(word2.get_next_reset_bits(8, &pcount), static_cast< uint8_t >(64));
    ASSERT_EQ(pcount, static_cast< uint8_t >(0));

    const Bitword< unsafe_bits< uint64_t > > word3{0x3FFFFFFFFFFFFFFF};
    ASSERT_EQ(word3.get_next_reset_bits(0, &pcount), static_cast< uint8_t >(62));
    ASSERT_EQ(pcount, static_cast< uint8_t >(2));
    ASSERT_EQ(word3.get_next_reset_bits(8, &pcount), static_cast< uint8_t >(62));
    ASSERT_EQ(pcount, static_cast< uint8_t >(2));
    ASSERT_EQ(word3.get_next_reset_bits(63, &pcount), static_cast< uint8_t >(63));
    ASSERT_EQ(pcount, static_cast< uint8_t >(1));

    const Bitword< unsafe_bits< uint64_t > > word4{0xFFFFFFFFFFFFFFFF};
    ASSERT_EQ(word4.get_next_reset_bits(0, &pcount), static_cast< uint8_t >(64));
    ASSERT_EQ(word4.get_next_reset_bits(8, &pcount), static_cast< uint8_t >(64));

    const Bitword< unsafe_bits< uint64_t > > word5{0x3FFFFFFFFFFFFFF0};
    ASSERT_EQ(word5.get_next_reset_bits(0, &pcount), static_cast< uint8_t >(0));
    ASSERT_EQ(pcount, static_cast< uint8_t >(4));
    ASSERT_EQ(word5.get_next_reset_bits(8, &pcount), static_cast< uint8_t >(62));
    ASSERT_EQ(pcount, static_cast< uint8_t >(2));
}

TEST_F(BitwordTest, SetNextResetBit) {
    uint8_t pBit;
    Bitword< unsafe_bits< uint64_t > > word1{0x00};
    ASSERT_TRUE(word1.set_next_reset_bit(0, 64, &pBit));
    ASSERT_EQ(pBit, static_cast< uint8_t >(0));
    ASSERT_TRUE(word1.set_next_reset_bit(1, 64, &pBit));
    ASSERT_EQ(pBit, static_cast< uint8_t >(1));

    Bitword< unsafe_bits< uint64_t > > word2{0x7FFFFFFFFFFFFFFF};
    ASSERT_TRUE(word2.set_next_reset_bit(0, 64, &pBit));
    ASSERT_EQ(pBit, static_cast< uint8_t >(63));
    ASSERT_FALSE(word2.set_next_reset_bit(1, 64, &pBit));

    Bitword< unsafe_bits< uint64_t > > word3{0x0FF};
    ASSERT_FALSE(word3.set_next_reset_bit(0, 8, &pBit));
}

TEST_F(BitwordTest, RightShift) {
    Bitword< unsafe_bits< uint64_t > > word1{0xFF00};
    ASSERT_EQ(word1.right_shift(8), static_cast< uint64_t >(0xFF));
}

TEST_F(BitwordTest, ToString) {
    const Bitword< unsafe_bits< uint8_t > > word1{0x0F};
    ASSERT_EQ(word1.to_string(), std::string{"00001111"});
}

TEST_F(BitwordTest, GetNextResetBitsFiltered) {
    ASSERT_TRUE(validate(0xfff0, 0, {5, 5, 1}, 16, bit_match_type::msb_match, 48));
    ASSERT_TRUE(validate(0xfff0, 0, {4, 5, 1}, 0, bit_match_type::lsb_match, 4));

    ASSERT_TRUE(validate(0x0, 0, {5, 5, 1}, 0, bit_match_type::full_match, 64));
    ASSERT_TRUE(validate(0x0, 0, {64, 70, 1}, 0, bit_match_type::full_match, 64));
    ASSERT_TRUE(validate(0xffffffffffffffff, 0, {5, 5, 1}, 0, bit_match_type::no_match, 0));

    ASSERT_TRUE(validate(0x7fffffffffffffff, 0, {2, 2, 1}, 63, bit_match_type::msb_match, 1));
    ASSERT_TRUE(validate(0x7f0f0f0f0f0f0f0f, 0, {2, 2, 1}, 4, bit_match_type::mid_match, 4));
    ASSERT_TRUE(validate(0x7f0f0f0f0f0f0f0f, 29, {2, 2, 1}, 29, bit_match_type::mid_match, 3));

    ASSERT_TRUE(validate(0x8000000000000000, 0, {5, 8, 1}, 0, bit_match_type::lsb_match, 63));
    ASSERT_TRUE(validate(0x8000000000000001, 0, {5, 8, 1}, 1, bit_match_type::mid_match, 62));
    ASSERT_TRUE(validate(0x8000000000000001, 10, {8, 8, 1}, 10, bit_match_type::mid_match, 53));

    ASSERT_TRUE(validate(0x7fffffffffffffff, 0, {1, 1, 1}, 63, bit_match_type::msb_match, 1));
    ASSERT_TRUE(validate(0x7fffffffffffffff, 56, {1, 1, 1}, 63, bit_match_type::msb_match, 1));
    ASSERT_TRUE(validate(0x7fffffffffffffff, 56, {2, 2, 1}, 63, bit_match_type::msb_match, 1));

    ASSERT_TRUE(validate(0x7ff000ffff00ff0f, 0, {11, 11, 1}, 40, bit_match_type::mid_match, 12));
    ASSERT_TRUE(validate(0x7ff000ffff00ff0f, 5, {2, 2, 1}, 5, bit_match_type::mid_match, 3));
    ASSERT_TRUE(validate(0x7ff000ffff00ff0f, 5, {8, 8, 1}, 16, bit_match_type::mid_match, 8));

    ASSERT_TRUE(validate(0x0ff000ffff00ff0f, 5, {8, 64, 4}, 60, bit_match_type::msb_match, 4));

    ASSERT_TRUE(validate(0x8fffff0f0f0f00f4, 0, {3, 9, 1}, 0, bit_match_type::no_match, 0));
    ASSERT_TRUE(validate(0x8ff00f0f0f0f00f4, 1, {3, 9, 1}, 0, bit_match_type::no_match, 0));
    ASSERT_TRUE(validate(0x7ff00f0f0f0f00f4, 0, {3, 9, 2}, 0, bit_match_type::no_match, 0));
    ASSERT_TRUE(validate(0x00ff0f0f0f0ff0f4, 0, {3, 9, 9}, 0, bit_match_type::no_match, 0));
}

TEST_F(BitwordTest, GetMaxContiguousResetBits) {
    uint8_t pmax_count;
    const Bitword< unsafe_bits< uint64_t > > word1{0xFFFFFFFFFFFFFFFF};
    ASSERT_EQ(word1.get_max_contiguous_reset_bits(0, &pmax_count), std::numeric_limits< uint8_t >::max());

    const Bitword< unsafe_bits< uint64_t > > word2{0xFFFFFFFFFFFFFFF0};
    ASSERT_EQ(word2.get_max_contiguous_reset_bits(0, &pmax_count), static_cast< uint8_t >(0));
    ASSERT_EQ(pmax_count, static_cast< uint8_t >(4));
    ASSERT_EQ(word2.get_max_contiguous_reset_bits(1, &pmax_count), static_cast< uint8_t >(1));
    ASSERT_EQ(pmax_count, static_cast< uint8_t >(3));

    const Bitword< unsafe_bits< uint64_t > > word3{0x0FFFFFFFFFFFFFFF};
    ASSERT_EQ(word3.get_max_contiguous_reset_bits(0, &pmax_count), static_cast< uint8_t >(60));
    ASSERT_EQ(pmax_count, static_cast< uint8_t >(4));
    ASSERT_EQ(word3.get_max_contiguous_reset_bits(1, &pmax_count), static_cast< uint8_t >(60));
    ASSERT_EQ(pmax_count, static_cast< uint8_t >(4));

    const Bitword< unsafe_bits< uint64_t > > word4{0xFFFFFFFFFFFFFF0F};
    ASSERT_EQ(word4.get_max_contiguous_reset_bits(0, &pmax_count), static_cast< uint8_t >(4));
    ASSERT_EQ(pmax_count, static_cast< uint8_t >(4));
    ASSERT_EQ(word4.get_max_contiguous_reset_bits(1, &pmax_count), static_cast< uint8_t >(4));
    ASSERT_EQ(pmax_count, static_cast< uint8_t >(4));
    ASSERT_EQ(word4.get_max_contiguous_reset_bits(8, &pmax_count), std::numeric_limits< uint8_t >::max());

    const Bitword< unsafe_bits< uint64_t > > word5{0xFF00FFFFFFFFFF0F};
    ASSERT_EQ(word5.get_max_contiguous_reset_bits(0, &pmax_count), static_cast< uint8_t >(48));
    ASSERT_EQ(pmax_count, static_cast< uint8_t >(8));

    const Bitword< unsafe_bits< uint64_t > > word6{0xFF00FFFFFFFF000F};
    ASSERT_EQ(word6.get_max_contiguous_reset_bits(0, &pmax_count), static_cast< uint8_t >(4));
    ASSERT_EQ(pmax_count, static_cast< uint8_t >(12));
}

int main(int argc, char* argv[]) {
    SISL_OPTIONS_LOAD(argc, argv, logging)
    ::testing::InitGoogleTest(&argc, argv);
    sisl::logging::SetLogger("test_bitword");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    return RUN_ALL_TESTS();
}
