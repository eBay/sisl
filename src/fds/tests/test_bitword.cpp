
#include <cstdint>
#include <iostream>
#include <limits>

#include <gtest/gtest.h>
#include <sds_logging/logging.h>
#include <sds_options/options.h>

#include "bitword.hpp"

using namespace sisl;

SDS_LOGGING_INIT(test_bitword);
SDS_OPTIONS_ENABLE(logging)

static bool validate(const uint64_t val, const uint8_t offset, bit_filter filter, const uint8_t exp_start,
                     const bit_match_type exp_match) {
    Bitword< unsafe_bits< uint64_t > > bword(val);

    const auto result{bword.get_next_reset_bits_filtered(offset, filter)};
    if (result.match_type != exp_match) {
        //LOGINFO("Val={} offset={} filter[{}] Expected type={} but got {}, result[{}]: FAILED", val, static_cast<uint16_t>(offset),
        //        filter.to_string(), static_cast<uint16_t>(exp_match), static_cast<uint16_t>(result.match_type), result.to_string());
        return false;
    }

    if ((result.match_type != bit_match_type::no_match) && (result.start_bit != exp_start)) {
        //LOGINFO("Val={} offset={} filter[{}] Expected start bit={} but got {}, result[{}] : FAILED", val, static_cast<uint16_t>(offset),
        //        filter.to_string(), static_cast<uint16_t>(exp_start), static_cast<uint16_t>(result.start_bit), result.to_string());
        return false;
    }

    //LOGINFO("Val={} offset={} filter[{}] result[{}] : Passed", val, static_cast<uint16_t>(offset), filter.to_string(), result.to_string());
    return true;
}

class BitwordTest : public testing::Test {
public:
    BitwordTest() : testing::Test() {};
    BitwordTest(const BitwordTest&) = delete;
    BitwordTest(BitwordTest&&) noexcept = delete;
    BitwordTest& operator=(const BitwordTest&) = delete;
    BitwordTest& operator=(BitwordTest&&) noexcept = delete;
    virtual ~BitwordTest() override = default;
   
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(BitwordTest, TestTrailingZeros)
{
    ASSERT_EQ(get_trailing_zeros(0x01), static_cast<uint8_t>(0));
    ASSERT_EQ(get_trailing_zeros(0x00), static_cast<uint8_t>(64));
    ASSERT_EQ(get_trailing_zeros(0xf000000000), static_cast<uint8_t>(36));
}

TEST_F(BitwordTest, TestSetCount) {
    const Bitword< unsafe_bits<uint64_t> > word1{0x1};
    ASSERT_EQ(word1.get_set_count(), 1);

    const Bitword< unsafe_bits< uint64_t > > word2{0x0};
    ASSERT_EQ(word2.get_set_count(), 0);

    const Bitword< unsafe_bits< uint64_t > > word3{0x100000000};
    ASSERT_EQ(word3.get_set_count(), 1);

    const Bitword< unsafe_bits< uint64_t > > word4{0xFFFFFFFFFFFFFFFF};
    ASSERT_EQ(word4.get_set_count(), 64);
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
    ASSERT_EQ(word1.set_reset_bit(0,true), static_cast<uint64_t>(0x01));
    ASSERT_EQ(word1.set_reset_bit(63, true), static_cast<uint64_t>(0x8000000000000001));

    ASSERT_EQ(word1.set_reset_bit(0, false), static_cast< uint64_t >(0x8000000000000000));
    ASSERT_EQ(word1.set_reset_bit(63, false), static_cast< uint64_t >(0x00));
}

TEST_F(BitwordTest, SetBits)
{
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
    ASSERT_EQ(bit_pos, static_cast<uint8_t>(0));
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

TEST_F(BitwordTest, GetNextResetBitsFiltered)
{
    ASSERT_TRUE(validate(0xfff0, 0, {5, 5, 1}, 16, bit_match_type::mid_match));
    ASSERT_TRUE(validate(0xfff0, 0, {4, 5, 1}, 0, bit_match_type::lsb_match));

    ASSERT_TRUE(validate(0x0, 0, {5, 5, 1}, 0, bit_match_type::lsb_match));
    ASSERT_TRUE(validate(0x0, 0, {64, 70, 1}, 0, bit_match_type::lsb_match));
    ASSERT_TRUE(validate(0xffffffffffffffff, 0, {5, 5, 1}, 0, bit_match_type::no_match));

    ASSERT_TRUE(validate(0x7fffffffffffffff, 0, {2, 2, 1}, 63, bit_match_type::msb_match));
    ASSERT_TRUE(validate(0x7f0f0f0f0f0f0f0f, 0, {2, 2, 1}, 4, bit_match_type::mid_match));

    ASSERT_TRUE(validate(0x8000000000000000, 0, {5, 8, 1}, 0, bit_match_type::lsb_match));
    ASSERT_TRUE(validate(0x8000000000000001, 0, {5, 8, 1}, 1, bit_match_type::mid_match));
    ASSERT_TRUE(validate(0x8000000000000001, 10, {8, 8, 1}, 10, bit_match_type::mid_match));

    ASSERT_TRUE(validate(0x7fffffffffffffff, 0, {1, 1, 1}, 63, bit_match_type::mid_match));
    ASSERT_TRUE(validate(0x7fffffffffffffff, 56, {1, 1, 1}, 63, bit_match_type::mid_match));
    ASSERT_TRUE(validate(0x7fffffffffffffff, 56, {2, 2, 1}, 63, bit_match_type::msb_match));

    ASSERT_TRUE(validate(0x7ff000ffff00ff0f, 0, {11, 11, 1}, 40, bit_match_type::mid_match));
    ASSERT_TRUE(validate(0x7ff000ffff00ff0f, 5, {2, 2, 1}, 5, bit_match_type::mid_match));
    ASSERT_TRUE(validate(0x7ff000ffff00ff0f, 5, {8, 8, 1}, 16, bit_match_type::mid_match));
}

TEST_F(BitwordTest, GetMaxContiguousResetBits) {
    uint8_t pmax_count;
    const Bitword< unsafe_bits< uint64_t > > word1{0xFFFFFFFFFFFFFFFF};
    ASSERT_EQ(word1.get_max_contiguous_reset_bits(0, &pmax_count), std::numeric_limits<uint8_t>::max());

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
    SDS_OPTIONS_LOAD(argc, argv, logging)
    ::testing::InitGoogleTest(&argc, argv);
    sds_logging::SetLogger("test_bitword");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    return RUN_ALL_TESTS();
}