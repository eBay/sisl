
#include <cstdint>
#include <iostream>

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
        LOGINFO("Val={} offset={} filter[{}] Expected type={} but got {}, result[{}]: FAILED", val, static_cast<uint16_t>(offset),
                filter.to_string(), static_cast<uint16_t>(exp_match), static_cast<uint16_t>(result.match_type), result.to_string());
        return false;
    }

    if ((result.match_type != bit_match_type::no_match) && (result.start_bit != exp_start)) {
        LOGINFO("Val={} offset={} filter[{}] Expected start bit={} but got {}, result[{}] : FAILED", val, static_cast<uint16_t>(offset),
                filter.to_string(), static_cast<uint16_t>(exp_start), static_cast<uint16_t>(result.start_bit), result.to_string());
        return false;
    }

    LOGINFO("Val={} offset={} filter[{}] result[{}] : Passed", val, static_cast<uint16_t>(offset), filter.to_string(), result.to_string());
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

TEST_F(BitwordTest, TestGetNextResetBitsFiltered)
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

int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging)
    ::testing::InitGoogleTest(&argc, argv);
    sds_logging::SetLogger("test_bitword");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    return RUN_ALL_TESTS();
}