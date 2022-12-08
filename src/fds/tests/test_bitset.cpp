/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam, Bryan Zimmerman
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
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include <boost/dynamic_bitset.hpp>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include <gtest/gtest.h>

#include "sisl/fds/bitset.hpp"

using namespace sisl;

SISL_LOGGING_INIT(test_bitset)

namespace {
uint64_t g_total_bits;
uint32_t g_num_threads;
uint32_t g_set_pct;
uint32_t g_max_bits_in_group;

class ShadowBitset {
public:
    ShadowBitset() {
        m_bitset.reserve(g_total_bits);
        m_bitset.resize(g_total_bits);
    }
    ShadowBitset(const ShadowBitset&) = delete;
    ShadowBitset(ShadowBitset&&) noexcept = delete;
    ShadowBitset& operator=(const ShadowBitset&) = delete;
    ShadowBitset& operator=(ShadowBitset&&) noexcept = delete;

    void set(const bool value, const uint64_t start_bit, const uint32_t total_bits = 1) {
        std::unique_lock l{m_mutex};
        for (auto b{start_bit}; b < start_bit + total_bits; ++b) {
            m_bitset[b] = value;
        }
    }

    uint64_t get_next_set_bit(const uint64_t start_bit) {
        std::unique_lock l{m_mutex};
        return (start_bit == boost::dynamic_bitset<>::npos) ? m_bitset.find_first() : m_bitset.find_next(start_bit);
    }

    uint64_t get_next_reset_bit(const uint64_t start_bit) {
        std::unique_lock l{m_mutex};
        return (start_bit == boost::dynamic_bitset<>::npos) ? (~m_bitset).find_first()
                                                            : (~m_bitset).find_next(start_bit);
    }

    sisl::BitBlock get_next_contiguous_n_reset_bits(const uint64_t start_bit, const uint32_t n) {
        sisl::BitBlock ret{boost::dynamic_bitset<>::npos, 0u};

        std::unique_lock l{m_mutex};
        // LOGINFO("ShadowBitset next reset request bit={}", start_bit);
        const uint64_t offset_bit{(start_bit == 0) ? (~m_bitset).find_first() : (~m_bitset).find_next(start_bit - 1)};
        for (auto b{offset_bit}; b < m_bitset.size(); ++b) {
            // LOGINFO("ShadowBitset next reset bit = {}, m_bitset[b]={}", offset_bit, m_bitset[offset_bit]);
            if (!m_bitset[b]) {
                if (ret.nbits == 0) {
                    ret.start_bit = b;
                    ret.nbits = 1;
                } else {
                    ++ret.nbits;
                }
                if (ret.nbits == n) { return ret; }
            } else {
                ret.nbits = 0;
            }
        }

        return ret;
    }

    void shrink_head(const uint64_t nbits) {
        std::unique_lock l{m_mutex};
        m_bitset >>= nbits;
        m_bitset.resize(m_bitset.size() - nbits);
    }

    void resize(const uint64_t new_size) {
        std::unique_lock l{m_mutex};
        m_bitset.resize(new_size);
    }

    bool is_set(const uint64_t bit) {
        std::unique_lock l{m_mutex};
        return m_bitset[bit];
    }

private:
    void set_or_reset(const uint64_t bit, const bool value) {
        std::unique_lock l{m_mutex};
        m_bitset[bit] = value;
    }

    std::mutex m_mutex;
    boost::dynamic_bitset<> m_bitset;
};

struct BitsetTest : public testing::Test {
public:
    BitsetTest() : testing::Test(), m_total_bits{g_total_bits}, m_bset{m_total_bits} {
        LOGINFO("Initializing new BitsetTest class");
    }
    BitsetTest(const BitsetTest&) = delete;
    BitsetTest(BitsetTest&&) noexcept = delete;
    BitsetTest& operator=(const BitsetTest&) = delete;
    BitsetTest& operator=(BitsetTest&&) noexcept = delete;
    virtual ~BitsetTest() override = default;

protected:
    uint64_t m_total_bits;
    ShadowBitset m_shadow_bm;
    sisl::ThreadSafeBitset m_bset;

    void SetUp() override {}
    void TearDown() override {}

    void fill_random(const uint64_t start, const uint32_t nbits) {
        static thread_local std::random_device rd{};
        static thread_local std::default_random_engine re{rd()};
        std::uniform_int_distribution< uint8_t > rand{0, 1};
        for (uint64_t bit{start}; bit < start + nbits; ++bit) {
            const bool set{rand(re) == 1};
            if (set) {
                m_bset.set_bit(bit);
                m_shadow_bm.set(true, bit);
            } else {
                m_bset.reset_bit(bit);
                m_shadow_bm.set(false, bit);
            }
        }
    }

    void set(const uint64_t start_bit, const uint32_t nbits) {
        m_bset.set_bits(start_bit, nbits);
        m_shadow_bm.set(true, start_bit, nbits);
    }

    void reset(const uint64_t start_bit, const uint32_t nbits) {
        m_bset.reset_bits(start_bit, nbits);
        m_shadow_bm.set(false, start_bit, nbits);
    }

    void shrink_head(const uint64_t nbits) {
        m_bset.shrink_head(nbits);
        m_shadow_bm.shrink_head(nbits);
        m_total_bits -= nbits;
    }

    void expand_tail(const uint64_t nbits) {
        m_bset.resize(m_total_bits + nbits);
        m_shadow_bm.resize(m_total_bits + nbits);
        m_total_bits += nbits;
    }

    void validate_all(const uint32_t n_continous_expected) {
        validate_by_simple_get();
        validate_by_next_bits(true);
        validate_by_next_bits(false);
        validate_by_next_continous_bits(n_continous_expected);
    }

    void validate_by_simple_get() {
        LOGINFO("INFO: Validate upto total bits {} by cross-checking every entity", m_total_bits);
        for (uint64_t i{0}; i < m_total_bits; ++i) {
            ASSERT_EQ(m_bset.get_bitval(i), m_shadow_bm.is_set(i)) << "Bit mismatch for bit=" << i;
        }
    }

    void validate_by_next_bits(const bool by_set = true) {
        LOGINFO("INFO: Validate upto total bits {} by checking next {} bit", m_total_bits, (by_set ? "set" : "reset"));
        uint64_t next_shadow_bit{boost::dynamic_bitset<>::npos};
        uint64_t next_bset_bit{0};

        do {
            const auto expected_bit{by_set ? m_shadow_bm.get_next_set_bit(next_shadow_bit)
                                           : m_shadow_bm.get_next_reset_bit(next_shadow_bit)};
            const auto actual_bit{by_set ? m_bset.get_next_set_bit(next_bset_bit)
                                         : m_bset.get_next_reset_bit(next_bset_bit)};

            if (expected_bit == boost::dynamic_bitset<>::npos) {
                ASSERT_EQ(actual_bit, Bitset::npos) << "Next " << (by_set ? "set" : "reset") << " bit after "
                                                    << next_bset_bit << " is expected to be EOB, but not";
                break;
            }
            ASSERT_EQ(expected_bit, actual_bit)
                << "Next " << (by_set ? "set" : "reset") << " bit after " << next_bset_bit << " is not expected ";
            next_bset_bit = actual_bit + 1;
            next_shadow_bit = expected_bit;
        } while (true);
    }

    void validate_by_next_continous_bits(const uint32_t n_continous = 1) {
        uint64_t next_start_bit{0};
        uint64_t n_retrival{0};

        LOGINFO("INFO: Validate upto total bits {} by checking n_continous={} reset bits", m_total_bits, n_continous);
        do {
            const auto expected{m_shadow_bm.get_next_contiguous_n_reset_bits(next_start_bit, n_continous)};
            const auto actual{m_bset.get_next_contiguous_n_reset_bits(next_start_bit, n_continous)};

            LOGTRACE("next continous bit for start_bit={}, expected.start_bit={}, expected.nbits={}, "
                     "actual.start_bit={}, actual.nbits={}",
                     next_start_bit, expected.start_bit, expected.nbits, actual.start_bit, actual.nbits);
            if (expected.nbits != n_continous) {
                ASSERT_NE(actual.nbits, n_continous)
                    << "Next continous reset bit after " << next_start_bit << " is expected to be EOB, but not";
                break;
            }

            ASSERT_EQ(expected.start_bit, actual.start_bit)
                << "Next continous reset bit after " << next_start_bit << " start_bit value is not expected ";
            ASSERT_EQ(expected.nbits, actual.nbits)
                << "Next continous reset bit after " << next_start_bit << " nbits value is not expected ";

            if (actual.nbits) { ++n_retrival; }
            next_start_bit = expected.start_bit + expected.nbits;
        } while (true);

        LOGINFO("Got total {} instances of continous bits {}", n_retrival, n_continous);
    }

public:
    uint64_t total_bits() const { return m_total_bits; }
};

void run_parallel(const uint64_t total_bits, const uint32_t nthreads,
                  const std::function< void(const uint64_t, const uint32_t) >& thr_fn) {
    uint64_t start{0};
    const uint32_t n_per_thread{static_cast< uint32_t >(std::ceil(static_cast< double >(total_bits) / nthreads))};
    std::vector< std::thread > threads;

    while (start < total_bits) {
        threads.emplace_back(thr_fn, start, std::min< uint32_t >(n_per_thread, total_bits - start));
        start += n_per_thread;
    }

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
}
} // namespace

TEST_F(BitsetTest, TestSetCountWithShift) {
    m_bset.set_bits(0, g_total_bits);
    ASSERT_EQ(m_bset.get_set_count(), g_total_bits);

    // test same first word
    ASSERT_EQ(m_bset.get_set_count(0, 0), static_cast< uint64_t >(1));
    ASSERT_EQ(m_bset.get_set_count(m_bset.word_size() - 1, m_bset.word_size() - 1), static_cast< uint64_t >(1));
    ASSERT_EQ(m_bset.get_set_count(m_bset.word_size() - 4, m_bset.word_size() - 1), static_cast< uint64_t >(4));
    ASSERT_EQ(m_bset.get_set_count(0, m_bset.word_size() - 1), m_bset.word_size());

    const uint64_t start1{static_cast< uint64_t >(m_bset.word_size() / 2)};
    ASSERT_EQ(m_bset.get_set_count(start1), g_total_bits - start1);
    const uint64_t start2{m_bset.word_size()};
    ASSERT_EQ(m_bset.get_set_count(start2), g_total_bits - start2);
    ASSERT_EQ(m_bset.get_set_count(0, g_total_bits - 1 - start1), g_total_bits - start1);
    ASSERT_EQ(m_bset.get_set_count(0, g_total_bits - 1 - start2), g_total_bits - start2);
    ASSERT_EQ(m_bset.get_set_count(start1, g_total_bits - 1 - start1), g_total_bits - 2 * start1);
    ASSERT_EQ(m_bset.get_set_count(start1, g_total_bits - 1 - start2), g_total_bits - start1 - start2);
    ASSERT_EQ(m_bset.get_set_count(0, g_total_bits - 1 - start2), g_total_bits - start2);
    ASSERT_EQ(m_bset.get_set_count(start2, g_total_bits - 1 - start1), g_total_bits - start1 - start2);

    // offset right a partial word
    const uint64_t offset1{4};
    shrink_head(offset1);

    // offset right more than a word
    const uint64_t offset2{static_cast< uint64_t >(2 * m_bset.word_size())};
    shrink_head(offset2);
    ASSERT_EQ(m_bset.get_set_count(), g_total_bits - (offset1 + offset2));

    // offset right an exact multiple of a word
    const uint64_t offset3{static_cast< uint64_t >(m_bset.word_size() - ((offset1 + offset2) % m_bset.word_size()))};
    shrink_head(offset3);
    ASSERT_EQ(m_bset.get_set_count(), g_total_bits - (offset1 + offset2 + offset3));

    // test same first word shifted
    m_bset.reset_bits(0, 2);
    ASSERT_EQ(m_bset.get_set_count(0, 1), static_cast< uint64_t >(0));
    shrink_head(2);
    ASSERT_EQ(m_bset.get_set_count(0, 0), static_cast< uint64_t >(1));
    ASSERT_EQ(m_bset.get_set_count(0, 61), static_cast< uint64_t >(62));
}

TEST_F(BitsetTest, TestSetCount) {
    m_bset.set_bits(0, g_total_bits);
    ASSERT_EQ(m_bset.get_set_count(), g_total_bits);

    // reset word bits aligned to word size
    const auto word_size{m_bset.word_size()};
    m_bset.reset_bits(0, word_size);
    ASSERT_EQ(m_bset.get_set_count(), g_total_bits - word_size);

    // reset word bits beginning and end of word and middle of word
    m_bset.reset_bits(2 * word_size, word_size / 2);
    ASSERT_EQ(m_bset.get_set_count(), g_total_bits - word_size - word_size / 2);
    m_bset.reset_bits(3 * word_size + word_size / 2, word_size / 2);
    ASSERT_EQ(m_bset.get_set_count(), g_total_bits - 2 * word_size);
    m_bset.reset_bits(4 * word_size + word_size / 4, word_size / 2);
    ASSERT_EQ(m_bset.get_set_count(), g_total_bits - 2 * word_size - word_size / 2);

    // reset multiple words
    m_bset.reset_bits(10 * word_size, 2 * word_size);
    ASSERT_EQ(m_bset.get_set_count(), g_total_bits - 4 * word_size - word_size / 2);
    m_bset.reset_bits(13 * word_size + word_size / 2, 2 * word_size);
    ASSERT_EQ(m_bset.get_set_count(), g_total_bits - 6 * word_size - word_size / 2);
}

TEST_F(BitsetTest, TestGetWordValue) {
    // use pointer constructor
    constexpr std::array< uint8_t, 16 > bits1{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                              0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    sisl::BitsetImpl< Bitword< unsafe_bits< uint8_t > >, true > bset1{bits1.data(), bits1.data() + bits1.size()};

    ASSERT_EQ(bset1.get_word_value(0), static_cast< uint8_t >(0x00));
    ASSERT_EQ(bset1.get_word_value(8), static_cast< uint8_t >(0x11));
    ASSERT_EQ(bset1.get_word_value(bits1.size() * 8 - 8), static_cast< uint8_t >(0xFF));

    // shift half word so composed of two words, data is ordered LSB to MSB per word
    constexpr uint64_t shrink_bits1{4};
    bset1.shrink_head(shrink_bits1);
    ASSERT_EQ(bset1.get_word_value(0), static_cast< uint8_t >(0x10));
    ASSERT_EQ(bset1.get_word_value(8), static_cast< uint8_t >(0x21));
    // get partial last word
    ASSERT_EQ(bset1.get_word_value(bits1.size() * 8 - shrink_bits1 - 4), static_cast< uint8_t >(0xF));

    // use iterator constructor
    const std::vector< uint8_t > bits2{0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
                                       0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00};
    sisl::BitsetImpl< Bitword< unsafe_bits< uint8_t > >, true > bset2{std::cbegin(bits2), std::cend(bits2)};

    ASSERT_EQ(bset2.get_word_value(0), static_cast< uint8_t >(0xFF));
    ASSERT_EQ(bset2.get_word_value(8), static_cast< uint8_t >(0xEE));
    ASSERT_EQ(bset2.get_word_value(bits2.size() * 8 - 8), static_cast< uint8_t >(0x00));

    // shift half word so composed of two words, data is ordered LSB to MSB per word
    constexpr uint64_t shrink_bits2{4};
    bset2.shrink_head(shrink_bits2);
    ASSERT_EQ(bset2.get_word_value(0), static_cast< uint8_t >(0xEF));
    ASSERT_EQ(bset2.get_word_value(8), static_cast< uint8_t >(0xDE));
    // get partial last word
    ASSERT_EQ(bset2.get_word_value(bits2.size() * 8 - shrink_bits2 - 4), static_cast< uint8_t >(0x00));

    constexpr std::array< uint64_t, 2 > bits3{0x0123456789ABCDEF, 0xFEDCBA9876543210};
    sisl::BitsetImpl< Bitword< unsafe_bits< uint64_t > >, true > bset3{bits3.data(), bits3.data() + bits3.size()};

    ASSERT_EQ(bset3.get_word_value(0), static_cast< uint64_t >(0x0123456789ABCDEF));
    ASSERT_EQ(bset3.get_word_value(64), static_cast< uint64_t >(0xFEDCBA9876543210));
    // get partial last word
    ASSERT_EQ(bset3.get_word_value(96), static_cast< uint64_t >(0xFEDCBA98));

    // shift a byte
    constexpr uint64_t shrink_bits3{8};
    bset3.shrink_head(shrink_bits3);
    ASSERT_EQ(bset3.get_word_value(0), static_cast< uint64_t >(0x100123456789ABCD));
}

TEST_F(BitsetTest, TestPrint) {
    const std::string str1{m_bset.to_string()};
    for (const char x : str1) {
        ASSERT_EQ(x, '0');
    }

    m_bset.set_bits(0, g_total_bits);
    const std::string str2{m_bset.to_string()};
    for (const char x : str2) {
        ASSERT_EQ(x, '1');
    }

    // test correct bit order of output
    constexpr std::array< uint64_t, 1 > bits1{0x0123456789ABCDEF};
    sisl::BitsetImpl< Bitword< unsafe_bits< uint64_t > >, true > bset1{bits1.data(), bits1.data() + bits1.size()};
    const std::string str3{bset1.to_string()};
    ASSERT_EQ(str3, std::string{"0000000100100011010001010110011110001001101010111100110111101111"});

    bset1.shrink_head(4);
    const std::string str4{bset1.to_string()};
    ASSERT_EQ(str4, std::string{"000000010010001101000101011001111000100110101011110011011110"});

    // test partial last word
    sisl::BitsetImpl< Bitword< unsafe_bits< uint64_t > >, true > bset2{32};
    bset2.set_bits(28, 4);
    bset2.set_bits(20, 4);
    bset2.set_bits(12, 4);
    bset2.set_bits(4, 4);
    const std::string str5{bset2.to_string()};
    ASSERT_EQ(str5, std::string{"11110000111100001111000011110000"});

    bset2.shrink_head(4);
    const std::string str6{bset2.to_string()};
    ASSERT_EQ(str6, std::string{"1111000011110000111100001111"});

    // test partial last word
    sisl::BitsetImpl< Bitword< unsafe_bits< uint32_t > >, true > bset3{36};
    bset3.set_bits(32, 4);
    bset3.set_bits(24, 4);
    bset3.set_bits(16, 4);
    bset3.set_bits(8, 4);
    bset3.set_bits(0, 4);
    const std::string str7{bset3.to_string()};
    ASSERT_EQ(str7, std::string{"111100001111000011110000111100001111"});

    bset3.shrink_head(4);
    const std::string str8{bset3.to_string()};
    ASSERT_EQ(str8, std::string{"11110000111100001111000011110000"});
}

TEST_F(BitsetTest, TestIsSetReset) {
    // test partial word lower half
    const uint8_t word_size{m_bset.word_size()};
    m_bset.set_bits(0, word_size / 2);
    ASSERT_TRUE(m_bset.is_bits_set(0, word_size / 2));

    // test partial word upper half
    m_bset.reset_bits(0, g_total_bits);
    const uint64_t start_bit1{static_cast< uint64_t >(word_size - (word_size / 2))};
    m_bset.set_bits(start_bit1, word_size / 2);
    ASSERT_TRUE(m_bset.is_bits_set(start_bit1, word_size / 2));

    // test half upper/lower next word
    m_bset.set_bits(word_size, word_size / 2);
    ASSERT_TRUE(m_bset.is_bits_set(start_bit1, word_size));
}

TEST_F(BitsetTest, TestCopyUnshifted) {
    // fill bitset with random data, shift, and make unshifted copy
    for (uint64_t shift{0}; shift <= m_bset.word_size(); ++shift) {
        const uint64_t total_bits{g_total_bits - shift};
        if (shift > 0) shrink_head(1);
        fill_random(0, total_bits);
        sisl::ThreadSafeBitset tmp_bset{};
        tmp_bset.copy_unshifted(m_bset);

        for (uint64_t bit{0}; bit < total_bits; ++bit) {
            ASSERT_EQ(m_bset.get_bitval(bit), tmp_bset.get_bitval(bit));
        }
    }
}

TEST_F(BitsetTest, GetNextContiguousUptoNResetBits) {
    m_bset.set_bits(0, g_total_bits);

    m_bset.reset_bits(1, 2);
    m_bset.reset_bits(64, 4);
    m_bset.reset_bits(127, 8);

    const auto result0{m_bset.get_next_contiguous_n_reset_bits(1, 1)};
    ASSERT_EQ(result0.start_bit, static_cast< uint64_t >(1));
    ASSERT_EQ(result0.nbits, static_cast< uint32_t >(1));

    const auto result1{m_bset.get_next_contiguous_n_reset_bits(0, 2)};
    ASSERT_EQ(result1.start_bit, static_cast< uint64_t >(1));
    ASSERT_EQ(result1.nbits, static_cast< uint32_t >(2));

    const auto result2{m_bset.get_next_contiguous_n_reset_bits(1, 2)};
    ASSERT_EQ(result2.start_bit, static_cast< uint64_t >(1));
    ASSERT_EQ(result2.nbits, static_cast< uint32_t >(2));

    const auto result3{m_bset.get_next_contiguous_n_reset_bits(0, 4)};
    ASSERT_EQ(result3.start_bit, static_cast< uint64_t >(64));
    ASSERT_EQ(result3.nbits, static_cast< uint32_t >(4));

    const auto result4{m_bset.get_next_contiguous_n_reset_bits(8, 4)};
    ASSERT_EQ(result4.start_bit, static_cast< uint64_t >(64));
    ASSERT_EQ(result4.nbits, static_cast< uint32_t >(4));

    const auto result5{m_bset.get_next_contiguous_n_reset_bits(0, 8)};
    ASSERT_EQ(result5.start_bit, static_cast< uint64_t >(127));
    ASSERT_EQ(result5.nbits, static_cast< uint32_t >(8));

    const auto result6{m_bset.get_next_contiguous_n_reset_bits(4, 8)};
    ASSERT_EQ(result6.start_bit, static_cast< uint64_t >(127));
    ASSERT_EQ(result6.nbits, static_cast< uint32_t >(8));

    const auto result7{m_bset.get_next_contiguous_n_reset_bits(70, 8)};
    ASSERT_EQ(result7.start_bit, static_cast< uint64_t >(127));
    ASSERT_EQ(result7.nbits, static_cast< uint32_t >(8));

    // test end bit out of range
    const auto result8{m_bset.get_next_contiguous_n_reset_bits(0, std::optional< uint64_t >{2 * g_total_bits}, 16, 16)};
    ASSERT_EQ(result8.start_bit, decltype(m_bset)::npos);
    ASSERT_EQ(result8.nbits, static_cast< uint32_t >(0));

    // test start out of range
    const auto result9{m_bset.get_next_contiguous_n_reset_bits(2 * g_total_bits, std::optional< uint64_t >{}, 16, 16)};
    ASSERT_EQ(result9.start_bit, decltype(m_bset)::npos);
    ASSERT_EQ(result9.nbits, static_cast< uint32_t >(0));

    // test null result
    const auto result10{m_bset.get_next_contiguous_n_reset_bits(0, std::optional< uint64_t >{}, 16, 16)};
    ASSERT_EQ(result10.start_bit, decltype(m_bset)::npos);
    ASSERT_EQ(result10.nbits, static_cast< uint32_t >(0));

    // test range of bits
    m_bset.reset_bits(256, 64);
    const auto result11{m_bset.get_next_contiguous_n_reset_bits(256, std::optional< uint64_t >{256 + 63}, 64, 64)};
    ASSERT_EQ(result11.start_bit, static_cast< uint64_t >(256));
    ASSERT_EQ(result11.nbits, static_cast< uint32_t >(64));
    const auto result12{m_bset.get_next_contiguous_n_reset_bits(256 + 32, std::optional< uint64_t >{256 + 63}, 32, 32)};
    ASSERT_EQ(result12.start_bit, static_cast< uint64_t >(256 + 32));
    ASSERT_EQ(result12.nbits, static_cast< uint32_t >(32));
    const auto result13{m_bset.get_next_contiguous_n_reset_bits(256, std::optional< uint64_t >{256 + 31}, 32, 32)};
    ASSERT_EQ(result13.start_bit, static_cast< uint64_t >(256));
    ASSERT_EQ(result13.nbits, static_cast< uint32_t >(32));
}

TEST_F(BitsetTest, EqualityLogicCheck) {
    // flip random bits
    const auto flip_random_bits{[this](auto& tmp_bitset, const size_t num_bits, const size_t num_flips) {
        static thread_local std::random_device rd{};
        static thread_local std::default_random_engine re{rd()};
        std::uniform_int_distribution< uint64_t > bit_rand{0, num_bits - 1};
        for (size_t flip{0}; flip < num_flips; ++flip) {
            const uint64_t bit{bit_rand(re)};

            if (tmp_bitset.get_bitval(bit)) {
                tmp_bitset.reset_bit(bit);
                ASSERT_FALSE(tmp_bitset.get_bitval(bit));
                ASSERT_FALSE(m_bset == tmp_bitset) << "Failed flipping bit " << bit << " out of " << num_bits;
                ASSERT_FALSE(tmp_bitset == m_bset) << "Failed flipping bit " << bit << " out of " << num_bits;
                tmp_bitset.set_bit(bit);
                ASSERT_TRUE(tmp_bitset.get_bitval(bit));
            } else {
                tmp_bitset.set_bit(bit);
                ASSERT_TRUE(tmp_bitset.get_bitval(bit));
                ASSERT_FALSE(m_bset == tmp_bitset) << "Failed flipping bit " << bit << " out of " << num_bits;
                ASSERT_FALSE(tmp_bitset == m_bset) << "Failed flipping bit " << bit << " out of " << num_bits;
                tmp_bitset.reset_bit(bit);
                ASSERT_FALSE(tmp_bitset.get_bitval(bit));
            }
        }
    }};

    // shift both equally through all alignments and test
    fill_random(0, g_total_bits);
    sisl::ThreadSafeBitset tmp_bset1{};
    tmp_bset1.copy(m_bset);
    uint64_t total_bits{g_total_bits};
    for (uint64_t shift{0}; shift <= m_bset.word_size(); ++shift) {
        if (shift > 0) {
            shrink_head(1);
            tmp_bset1.shrink_head(1);
            --total_bits;
        }
        ASSERT_TRUE(m_bset == tmp_bset1);
        ASSERT_TRUE(tmp_bset1 == m_bset);
        flip_random_bits(tmp_bset1, total_bits, total_bits / 20);
        ASSERT_TRUE(m_bset == tmp_bset1);
        ASSERT_TRUE(tmp_bset1 == m_bset);
    }

    // test shifted against unshifted through all alignments
    sisl::ThreadSafeBitset tmp_bset2{};
    for (uint64_t shift{0}; shift < m_bset.word_size(); ++shift) {
        shrink_head(1);
        --total_bits;
        tmp_bset2.copy_unshifted(m_bset);

        ASSERT_TRUE(m_bset == tmp_bset2);
        ASSERT_TRUE(tmp_bset2 == m_bset);
        flip_random_bits(tmp_bset2, total_bits, total_bits / 20);
        ASSERT_TRUE(m_bset == tmp_bset2);
        ASSERT_TRUE(tmp_bset2 == m_bset);
    }
}

TEST_F(BitsetTest, RandomSetAndShrink) {
    run_parallel(total_bits(), g_num_threads, [&](const uint64_t start, const uint32_t count) {
        static thread_local std::random_device s_rd{};
        static thread_local std::default_random_engine s_engine{s_rd()};
        std::uniform_int_distribution< uint32_t > pct_gen{0, 99};

        LOGINFO("INFO: Setting/Resetting all bits in range[{} - {}] set_pct={}", start, start + count - 1, g_set_pct);
        for (auto i{start}; i < start + count; ++i) {
            (pct_gen(s_engine) < g_set_pct) ? set(i, 1) : reset(i, 1);
        }
    });

    validate_all(1);

    LOGINFO("INFO: Truncate the size by 129 bits");
    shrink_head(129);

    run_parallel(total_bits(), g_num_threads, [&](const uint64_t start, const uint32_t count) {
        static thread_local std::random_device s_rd{};
        static thread_local std::default_random_engine s_engine{s_rd()};
        std::uniform_int_distribution< uint32_t > pct_gen{0, 99};

        LOGINFO("INFO: Setting/Resetting all bits in range[{} - {}] set_pct={}", start, start + count - 1, g_set_pct);
        for (auto i{start}; i < start + count; ++i) {
            (pct_gen(s_engine) < g_set_pct) ? set(i, 1) : reset(i, 1);
        }
    });
    validate_all(3);
}

TEST_F(BitsetTest, RandomMultiSetAndShrinkExpandToBoundaries) {
    run_parallel(total_bits(), g_num_threads, [&](const uint64_t start, const uint32_t count) {
        static thread_local std::random_device s_rd{};
        static thread_local std::default_random_engine s_engine{s_rd()};
        std::uniform_int_distribution< uint32_t > pct_gen{0, 99};
        std::uniform_int_distribution< uint32_t > bits_gen{0, g_max_bits_in_group - 1};
        std::uniform_int_distribution< uint64_t > bit_gen{0, count - 1};

        const uint32_t iterations{count / g_max_bits_in_group};
        LOGINFO("INFO: Setting/Resetting random bits (upto {} max) in range[{} - {}] with set_pct={} for {} iterations",
                g_max_bits_in_group, start, start + count - 1, g_set_pct, iterations);

        for (uint32_t iter{0}; iter < iterations; ++iter) {
            const uint64_t op_bit{bit_gen(s_engine) + start};
            const auto op_count{std::min< uint32_t >(bits_gen(s_engine) + 1, start + count - op_bit)};
            (pct_gen(s_engine) < g_set_pct) ? set(op_bit, op_count) : reset(op_bit, op_count);
        }
    });

    validate_all(1);
    validate_by_next_continous_bits(9);

    LOGINFO("INFO: Shrink the size by {} bits and then try to obtain 10 and 1 contigous entries", total_bits() - 1);
    shrink_head(total_bits() - 1);
    validate_all(10);
    validate_by_next_continous_bits(1);

    LOGINFO("INFO: Empty the bitset and then try to obtain 5 and 1 contigous entries");
    shrink_head(1);
    validate_all(5);
    validate_by_next_continous_bits(1);

    LOGINFO("INFO: Expand the bitset to {} and set randomly similar to earlier", g_total_bits / 2);
    expand_tail(g_total_bits / 2);
    run_parallel(total_bits(), g_num_threads, [&](const uint64_t start, const uint32_t count) {
        static thread_local std::random_device s_rd{};
        static thread_local std::default_random_engine s_engine{s_rd()};
        std::uniform_int_distribution< uint32_t > pct_gen{0, 99};
        std::uniform_int_distribution< uint32_t > bits_gen{0, g_max_bits_in_group - 1};
        std::uniform_int_distribution< uint64_t > bit_gen{0, count - 1};

        const uint32_t iterations{count / g_max_bits_in_group};
        LOGINFO("INFO: Setting/Resetting random bits (upto {} max) in range[{} - {}] with set_pct={} for {} iterations",
                g_max_bits_in_group, start, start + count - 1, g_set_pct, iterations);

        for (uint32_t iter{0}; iter < iterations; ++iter) {
            const uint64_t op_bit{bit_gen(s_engine) + start};
            const auto op_count{std::min< uint32_t >(bits_gen(s_engine) + 1, start + count - op_bit)};
            (pct_gen(s_engine) < g_set_pct) ? set(op_bit, op_count) : reset(op_bit, op_count);
        }
    });
    validate_all(3);
    validate_by_next_continous_bits(10);

    LOGINFO("INFO: Empty the bitset again and then try to obtain 5 and 1 contigous entries");
    shrink_head(total_bits());
    validate_all(1);
}

TEST_F(BitsetTest, SerializeDeserializePODBitset) {
    const auto fill_random{[](auto& bitset) {
        static thread_local std::random_device rd{};
        static thread_local std::default_random_engine re{rd()};
        std::uniform_int_distribution< uint8_t > rand{0, 1};
        const uint64_t nbits{bitset.size()};
        for (uint64_t bit{0}; bit < nbits; ++bit) {
            const bool set{rand(re) == 1};
            if (set) {
                bitset.set_bit(bit);
            } else {
                bitset.reset_bit(bit);
            }
        }
    }};

    // test 0 aligned bitsets
    Bitset bset1{1000};
    fill_random(bset1);
    Bitset bset2{};
    bset2.copy(bset1);
    const auto b1{bset1.serialize(0, false)};
    bset1 = Bitset{b1};
    EXPECT_EQ(bset1, bset2);
    const auto b2{bset1.serialize(0, true)};
    bset1 = Bitset{b2};
    EXPECT_EQ(bset1, bset2);

    // shift whole word
    bset1.shrink_head(Bitset::word_size());
    bset2.copy(bset1);
    const auto b3{bset1.serialize(0, false)};
    bset1 = Bitset{b3};
    EXPECT_EQ(bset1, bset2);
    const auto b4{bset1.serialize(0, true)};
    bset1 = Bitset{b4};
    EXPECT_EQ(bset1, bset2);

    // shift half word
    bset1.shrink_head(Bitset::word_size() / 2);
    bset2.copy(bset1);
    const auto b5{bset1.serialize(0, false)};
    bset1 = Bitset{b5};
    EXPECT_EQ(bset1, bset2);
    const auto b6{bset1.serialize(0, true)};
    bset1 = Bitset{b6};
    EXPECT_EQ(bset1, bset2);

    // shift one bit
    bset1.shrink_head(1);
    bset2.copy(bset1);
    const auto b7{bset1.serialize(0, false)};
    bset1 = Bitset{b7};
    EXPECT_EQ(bset1, bset2);
    const auto b8{bset1.serialize(0, true)};
    bset1 = Bitset{b8};
    EXPECT_EQ(bset1, bset2);

    // test aligned bitsets
    Bitset bset3{1000, 0, 512};
    fill_random(bset3);
    bset2.copy(bset3);
    const auto ab1{bset3.serialize(0, false)};
    bset3 = Bitset{ab1};
    EXPECT_EQ(bset3, bset2);
    const auto ab2{bset3.serialize(0, true)};
    bset3 = Bitset{ab2};
    EXPECT_EQ(bset3, bset2);

    // shif whole word
    bset3.shrink_head(Bitset::word_size());
    bset2.copy(bset3);
    const auto ab3{bset3.serialize(0, false)};
    bset3 = Bitset{ab3};
    EXPECT_EQ(bset3, bset2);
    const auto ab4{bset3.serialize(0, true)};
    bset3 = Bitset{ab4};
    EXPECT_EQ(bset3, bset2);

    // shift half word
    bset3.shrink_head(Bitset::word_size() / 2);
    bset2.copy(bset3);
    const auto ab5{bset3.serialize(0, false)};
    bset3 = Bitset{ab5};
    EXPECT_EQ(bset3, bset2);
    const auto ab6{bset3.serialize(0, true)};
    bset3 = Bitset{ab6};
    EXPECT_EQ(bset3, bset2);

    // shift one bit
    bset3.shrink_head(1);
    bset2.copy(bset3);
    const auto ab7{bset3.serialize(0, false)};
    bset3 = Bitset{ab7};
    EXPECT_EQ(bset3, bset2);
    const auto ab8{bset3.serialize(0, true)};
    bset3 = Bitset{ab8};
    EXPECT_EQ(bset3, bset2);

    // serialize different alignment
    bset2.copy(bset1);
    const auto dab1{bset1.serialize(512, false)};
    bset1 = Bitset{dab1};
    EXPECT_EQ(bset1, bset2);
    const auto dab2{bset1.serialize(512, true)};
    bset1 = Bitset{dab2};
    EXPECT_EQ(bset1, bset2);

    // reserialize different alignment
    bset2.copy(bset1);
    const auto dab3{bset1.serialize(0, false)};
    bset1 = Bitset{dab3, 512};
    EXPECT_EQ(bset1, bset2);
    const auto dab4{bset1.serialize(0, true)};
    bset1 = Bitset{dab4, 512};
    EXPECT_EQ(bset1, bset2);
}

TEST_F(BitsetTest, SerializeDeserializeAtomicBitset) {
    const auto fill_random{[](auto& bitset) {
        static thread_local std::random_device rd{};
        static thread_local std::default_random_engine re{rd()};
        std::uniform_int_distribution< uint8_t > rand{0, 1};
        const uint64_t nbits{bitset.size()};
        for (uint64_t bit{0}; bit < nbits; ++bit) {
            const bool set{rand(re) == 1};
            if (set) {
                bitset.set_bit(bit);
            } else {
                bitset.reset_bit(bit);
            }
        }
    }};

    // test 0 aligned bitsets
    AtomicBitset bset1{1000};
    fill_random(bset1);
    AtomicBitset bset2{};
    bset2.copy(bset1);
    const auto b1{bset1.serialize(0, false)};
    bset1 = AtomicBitset{b1};
    EXPECT_EQ(bset1, bset2);
    const auto b2{bset1.serialize(0, true)};
    bset1 = AtomicBitset{b2};
    EXPECT_EQ(bset1, bset2);

    // shif whole word
    bset1.shrink_head(AtomicBitset::word_size());
    bset2.copy(bset1);
    const auto b3{bset1.serialize(0, false)};
    bset1 = AtomicBitset{b3};
    EXPECT_EQ(bset1, bset2);
    const auto b4{bset1.serialize(0, true)};
    bset1 = AtomicBitset{b4};
    EXPECT_EQ(bset1, bset2);

    // shift half word
    bset1.shrink_head(AtomicBitset::word_size() / 2);
    bset2.copy(bset1);
    const auto b5{bset1.serialize(0, false)};
    bset1 = AtomicBitset{b5};
    EXPECT_EQ(bset1, bset2);
    const auto b6{bset1.serialize(0, true)};
    bset1 = AtomicBitset{b6};
    EXPECT_EQ(bset1, bset2);

    // shift one bit
    bset1.shrink_head(1);
    bset2.copy(bset1);
    const auto b7{bset1.serialize(0, false)};
    bset1 = AtomicBitset{b7};
    EXPECT_EQ(bset1, bset2);
    const auto b8{bset1.serialize(0, true)};
    bset1 = AtomicBitset{b8};
    EXPECT_EQ(bset1, bset2);

    // test aligned bitsets
    AtomicBitset bset3{1000, 0, 512};
    fill_random(bset3);
    bset2.copy(bset3);
    const auto ab1{bset3.serialize(0, false)};
    bset3 = AtomicBitset{ab1};
    EXPECT_EQ(bset3, bset2);
    const auto ab2{bset3.serialize(0, true)};
    bset3 = AtomicBitset{ab2};
    EXPECT_EQ(bset3, bset2);

    // shif whole word
    bset3.shrink_head(AtomicBitset::word_size());
    bset2.copy(bset3);
    const auto ab3{bset3.serialize(0, false)};
    bset3 = AtomicBitset{ab3};
    EXPECT_EQ(bset3, bset2);
    const auto ab4{bset3.serialize(0, true)};
    bset3 = AtomicBitset{ab4};
    EXPECT_EQ(bset3, bset2);

    // shift half word
    bset3.shrink_head(AtomicBitset::word_size() / 2);
    bset2.copy(bset3);
    const auto ab5{bset3.serialize(0, false)};
    bset3 = AtomicBitset{ab5};
    EXPECT_EQ(bset3, bset2);
    const auto ab6{bset3.serialize(0, true)};
    bset3 = AtomicBitset{ab6};
    EXPECT_EQ(bset3, bset2);

    // shift one bit
    bset3.shrink_head(1);
    bset2.copy(bset3);
    const auto ab7{bset3.serialize(0, false)};
    bset3 = AtomicBitset{ab7};
    EXPECT_EQ(bset3, bset2);
    const auto ab8{bset3.serialize(0, true)};
    bset3 = AtomicBitset{ab8};
    EXPECT_EQ(bset3, bset2);

    // serialize different alignment
    bset2.copy(bset1);
    const auto dab1{bset1.serialize(512, false)};
    bset1 = AtomicBitset{dab1};
    EXPECT_EQ(bset1, bset2);
    const auto dab2{bset1.serialize(512, true)};
    bset1 = AtomicBitset{dab2};
    EXPECT_EQ(bset1, bset2);

    // reserialize different alignment
    bset2.copy(bset1);
    const auto dab3{bset1.serialize(0, false)};
    bset1 = AtomicBitset{dab3, 512};
    EXPECT_EQ(bset1, bset2);
    const auto dab4{bset1.serialize(0, true)};
    bset1 = AtomicBitset{dab4, 512};
    EXPECT_EQ(bset1, bset2);
}

SISL_OPTIONS_ENABLE(logging, test_bitset)

SISL_OPTION_GROUP(test_bitset,
                  (num_threads, "", "num_threads", "number of threads",
                   ::cxxopts::value< uint32_t >()->default_value("8"), "number"),
                  (num_bits, "", "num_bits", "number of bits to start",
                   ::cxxopts::value< uint32_t >()->default_value("1000"), "number"),
                  (set_pct, "", "set_pct", "set percentage for randome test",
                   ::cxxopts::value< uint32_t >()->default_value("25"), "number"),
                  (max_bits_in_grp, "", "max_bits_in_grp", "max bits to be set/reset at a time",
                   ::cxxopts::value< uint32_t >()->default_value("72"), "number"))

int main(int argc, char* argv[]) {
    int parsed_argc{argc};
    ::testing::InitGoogleTest(&parsed_argc, argv);
    SISL_OPTIONS_LOAD(parsed_argc, argv, logging, test_bitset);
    sisl::logging::SetLogger("test_bitset");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    g_total_bits = SISL_OPTIONS["num_bits"].as< uint32_t >();
    g_num_threads = SISL_OPTIONS["num_threads"].as< uint32_t >();
    g_set_pct = SISL_OPTIONS["set_pct"].as< uint32_t >();
    g_max_bits_in_group = SISL_OPTIONS["set_pct"].as< uint32_t >();

    const auto ret{RUN_ALL_TESTS()};
    return ret;
}
