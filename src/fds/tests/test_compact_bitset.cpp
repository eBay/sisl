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
#include <cstdint>
#include <iostream>
#include <boost/dynamic_bitset.hpp>
#include <random>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include <gtest/gtest.h>

#include <sisl/fds/compact_bitset.hpp>

using namespace sisl;

SISL_OPTIONS_ENABLE(logging, test_compact_bitset)

class CompactBitsetTest : public testing::Test {
protected:
    sisl::io_blob_safe m_buf;
    std::unique_ptr< CompactBitSet > m_bset;

public:
    CompactBitsetTest() :
            testing::Test(),
            m_buf{uint32_cast(
                sisl::round_up(SISL_OPTIONS["buf_size"].as< uint32_t >(), CompactBitSet::size_multiples()))} {}
    CompactBitsetTest(const CompactBitsetTest&) = delete;
    CompactBitsetTest(CompactBitsetTest&&) noexcept = delete;
    CompactBitsetTest& operator=(const CompactBitsetTest&) = delete;
    CompactBitsetTest& operator=(CompactBitsetTest&&) noexcept = delete;
    virtual ~CompactBitsetTest() override = default;

protected:
    void SetUp() override { m_bset = std::make_unique< CompactBitSet >(m_buf, true); }
    void TearDown() override {}
};

TEST_F(CompactBitsetTest, AlternateBits) {
    ASSERT_EQ(m_bset->size(), m_buf.size() * 8);

    for (CompactBitSet::bit_count_t i{0}; i < m_bset->size(); ++i) {
        ASSERT_EQ(m_bset->is_bit_set(i), false);
    }

    // Set alternate bits
    for (CompactBitSet::bit_count_t i{0}; i < m_bset->size(); i += 2) {
        m_bset->set_bit(i);
    }

    for (CompactBitSet::bit_count_t i{0}; i < m_bset->size(); ++i) {
        ASSERT_EQ(m_bset->is_bit_set(i), (i % 2 == 0));
    }

    // Validate if next set or reset bit starting from itself returns itself back
    for (CompactBitSet::bit_count_t i{0}; i < m_bset->size(); ++i) {
        ASSERT_EQ(m_bset->get_next_set_or_reset_bit(i, ((i % 2) == 0)), i);
    }

    // Validate if next set or reset bit starting from previous returns next bit
    for (CompactBitSet::bit_count_t i{1}; i < m_bset->size(); ++i) {
        ASSERT_EQ(m_bset->get_next_set_or_reset_bit(i - 1, ((i % 2) == 0)), i);
    }
}

TEST_F(CompactBitsetTest, AllBits) {
    // Set all bits
    for (CompactBitSet::bit_count_t i{0}; i < m_bset->size(); ++i) {
        m_bset->set_bit(i);
    }

    for (CompactBitSet::bit_count_t i{0}; i < m_bset->size(); ++i) {
        ASSERT_EQ(m_bset->is_bit_set(i), true);
    }

    for (CompactBitSet::bit_count_t i{0}; i < m_bset->size(); ++i) {
        ASSERT_EQ(m_bset->get_next_set_bit(i), i);
        ASSERT_EQ(m_bset->get_next_reset_bit(i), CompactBitSet::inval_bit);
    }
}

TEST_F(CompactBitsetTest, RandomBitsWithReload) {
    auto const num_bits = m_bset->size();
    boost::dynamic_bitset<> shadow_bset{num_bits};

    std::random_device rd;
    std::mt19937 re(rd());
    std::uniform_int_distribution< CompactBitSet::bit_count_t > bit_gen(0, num_bits - 1);
    for (uint64_t i{0}; i < num_bits / 2; ++i) {
        auto bit = bit_gen(re);
        shadow_bset.set(bit);
        m_bset->set_bit(s_cast< CompactBitSet::bit_count_t >(bit));
    }

    auto validate = [this, &shadow_bset]() {
        CompactBitSet::bit_count_t prev_set_bit{CompactBitSet::inval_bit};
        for (uint64_t i{0}; i < m_bset->size(); ++i) {
            auto next_shadow_set_bit = (i == 0) ? shadow_bset.find_first() : shadow_bset.find_next(i - 1);
            CompactBitSet::bit_count_t next_set_bit = m_bset->get_next_set_bit(i);
            if (next_shadow_set_bit == boost::dynamic_bitset<>::npos) {
                ASSERT_EQ(next_set_bit, CompactBitSet::inval_bit);
            } else {
                ASSERT_EQ(next_set_bit, next_shadow_set_bit);
                if (next_set_bit == i) { prev_set_bit = i; }
                ASSERT_EQ(m_bset->get_prev_set_bit(i), prev_set_bit);
            }
        }

        // Flip it back so we can look for reset bits
        shadow_bset = shadow_bset.flip();
        for (uint64_t i{0}; i < m_bset->size(); ++i) {
            auto next_shadow_reset_bit = (i == 0) ? shadow_bset.find_first() : shadow_bset.find_next(i - 1);
            CompactBitSet::bit_count_t next_reset_bit = m_bset->get_next_reset_bit(i);
            if (next_shadow_reset_bit == boost::dynamic_bitset<>::npos) {
                ASSERT_EQ(next_reset_bit, CompactBitSet::inval_bit);
            } else {
                ASSERT_EQ(next_reset_bit, next_shadow_reset_bit);
            }
        }

        // Flip it back to original
        shadow_bset = shadow_bset.flip();
    };

    validate();
    m_bset = std::make_unique< CompactBitSet >(m_buf, false); // Reload
    validate();
}

SISL_OPTION_GROUP(test_compact_bitset,
                  (buf_size, "", "buf_size", "buf_size that contains the bits",
                   ::cxxopts::value< uint32_t >()->default_value("1024"), "number"))

int main(int argc, char* argv[]) {
    int parsed_argc{argc};
    ::testing::InitGoogleTest(&parsed_argc, argv);
    SISL_OPTIONS_LOAD(parsed_argc, argv, logging, test_compact_bitset);

    sisl::logging::SetLogger("test_compact_bitset");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    return RUN_ALL_TESTS();
}
