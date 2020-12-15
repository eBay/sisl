//
// Created by Kadayam, Hari on Sept 25 2019
//

#include <cstdint>
#include <memory>
#include <random>
#include <thread>
#include <vector>


#include <boost/dynamic_bitset.hpp>
#include <sds_logging/logging.h>
#include <sds_options/options.h>

#include <gtest/gtest.h>

#include "bitset.hpp"

using namespace sisl;

SDS_LOGGING_INIT(test_bitset)

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
        std::unique_lock l(m_mutex);
        for (auto b = start_bit; b < start_bit + total_bits; ++b) {
            m_bitset[b] = value;
        }
    }

    uint64_t get_next_set_bit(const uint64_t start_bit) {
        std::unique_lock l(m_mutex);
        return (start_bit == boost::dynamic_bitset<>::npos) ? m_bitset.find_first() : m_bitset.find_next(start_bit);
    }

    uint64_t get_next_reset_bit(const uint64_t start_bit) {
        std::unique_lock l(m_mutex);
        return (start_bit == boost::dynamic_bitset<>::npos) ? (~m_bitset).find_first()
                                                            : (~m_bitset).find_next(start_bit);
    }

    sisl::BitBlock get_next_contiguous_n_reset_bits(const uint64_t start_bit, const uint32_t n) {
        sisl::BitBlock ret{boost::dynamic_bitset<>::npos, 0u};

        std::unique_lock l(m_mutex);
        // LOGINFO("ShadowBitset next reset request bit={}", start_bit);
        const uint64_t offset_bit{(start_bit == 0) ? (~m_bitset).find_first() : (~m_bitset).find_next(start_bit - 1)};
        for (auto b = offset_bit; b < m_bitset.size(); ++b) {
            // LOGINFO("ShadowBitset next reset bit = {}, m_bitset[b]={}", offset_bit, m_bitset[offset_bit]);
            if (!m_bitset[b]) {
                if (ret.nbits == 0) {
                    ret.start_bit = b;
                    ret.nbits = 1;
                } else {
                    ret.nbits++;
                }
                if (ret.nbits == n) { return ret; }
            } else {
                ret.nbits = 0;
            }
        }

        return ret;
    }

    void shrink_head(const uint64_t nbits) {
        std::unique_lock l(m_mutex);
        m_bitset >>= nbits;
        m_bitset.resize(m_bitset.size() - nbits);
    }

    void resize(const uint64_t new_size) {
        std::unique_lock l(m_mutex);
        m_bitset.resize(new_size);
    }

    bool is_set(const uint64_t bit) {
        std::unique_lock l(m_mutex);
        return m_bitset[bit];
    }

private:
    void set_or_reset(const uint64_t bit, const bool value) {
        std::unique_lock l(m_mutex);
        m_bitset[bit] = value;
    }

    std::mutex m_mutex;
    boost::dynamic_bitset<> m_bitset;
};

struct BitsetTest : public testing::Test {
public:
    BitsetTest() : testing::Test(), m_total_bits(g_total_bits), m_bset(m_total_bits) {
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

    sisl::byte_array serialize() { return m_bset.serialize(); }

    void deserialize(sisl::byte_array buf) {
        sisl::ThreadSafeBitset tmp_bset(buf);
        m_bset = std::move(tmp_bset);
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
            auto expected_bit = by_set ? m_shadow_bm.get_next_set_bit(next_shadow_bit)
                                       : m_shadow_bm.get_next_reset_bit(next_shadow_bit);
            auto actual_bit =
                by_set ? m_bset.get_next_set_bit(next_bset_bit) : m_bset.get_next_reset_bit(next_bset_bit);

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

TEST_F(BitsetTest, TestSetCount)
{
    m_bset.set_bits(0, g_total_bits);
    ASSERT_EQ(m_bset.get_set_count(), g_total_bits);
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
}

TEST_F(BitsetTest, GetNextContiguousUptoNResetBits) {
    m_bset.set_bits(0, g_total_bits);
    m_bset.reset_bits(1, 2);
    m_bset.reset_bits(64, 4);
    m_bset.reset_bits(127, 8);

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
    const auto result8{m_bset.get_next_contiguous_n_reset_bits(0, std::optional< uint64_t >{ 2 * g_total_bits}, 16, 16)};
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
}

TEST_F(BitsetTest, AlternateSetAndShrink) {
    run_parallel(total_bits(), g_num_threads, [&](const uint64_t start, const uint32_t count) {
        LOGINFO("INFO: Setting alternate bits (set even and reset odd) in range[{} - {}]", start, start + count - 1);
        for (auto i{start}; i < start + count; ++i) {
            (i % 2 == 0) ? set(i, 1) : reset(i, 1);
        }
    });
    validate_all(1);

    LOGINFO("INFO: Shrink and right shift by 15 bits");
    shrink_head(15);

    run_parallel(total_bits(), g_num_threads, [&](const uint64_t start, const uint32_t count) {
        LOGINFO("INFO: Now toggle set/reset (reset even and set odd) in range[{} - {}]", start, start + count - 1);
        for (auto i{start}; i < start + count; ++i) {
            (i % 2 == 1) ? set(i, 1) : reset(i, 1);
        }
    });

    validate_all(1);
}

TEST_F(BitsetTest, SequentialSetAndExpand) {
    LOGINFO("INFO: Setting all bits from {} to end {}", total_bits() / 2, total_bits());
    run_parallel(total_bits(), g_num_threads, [&](const uint64_t start, const uint32_t count) {
        for (auto i{start}; i < start + count; ++i) {
            (i > total_bits() / 2) ? set(i, 1) : reset(i, 1);
        }
    });
    validate_all(1);

    LOGINFO("INFO: Increase the size by 1000 bits");
    expand_tail(1000);

    LOGINFO("INFO: Setting all bits from 0 to 200");
    run_parallel(total_bits(), g_num_threads, [&](const uint64_t start, const uint32_t count) {
        for (auto i{start}; i < start + count; ++i) {
            if (i <= 200) { set(i, 1); }
        }
    });

    validate_all(6);
    validate_by_next_continous_bits(161);
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
            (pct_gen(s_engine) < g_set_pct)  ? set(i, 1) : reset(i, 1);
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

        for(uint32_t iter{0}; iter < iterations; ++iter) {
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

        for(uint32_t iter{0}; iter < iterations; ++iter) {
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

TEST_F(BitsetTest, SerializeDeserialize) {
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
    shrink_head(139);

    LOGINFO("INFO: Serialize and then deserialize the bitset and then validate");
    auto b{serialize()};
    deserialize(b);
    validate_all(1);

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

SDS_OPTIONS_ENABLE(logging, test_bitset)

SDS_OPTION_GROUP(test_bitset,
                 (num_threads, "", "num_threads", "number of threads",
                  ::cxxopts::value< uint32_t >()->default_value("8"), "number"),
                 (num_bits, "", "num_bits", "number of bits to start",
                  ::cxxopts::value< uint32_t >()->default_value("1000"), "number"),
                 (set_pct, "", "set_pct", "set percentage for randome test",
                  ::cxxopts::value< uint32_t >()->default_value("25"), "number"),
                 (max_bits_in_grp, "", "max_bits_in_grp", "max bits to be set/reset at a time",
                  ::cxxopts::value< uint32_t >()->default_value("72"), "number"))

int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging, test_bitset);
    ::testing::InitGoogleTest(&argc, argv);
    sds_logging::SetLogger("test_bitset");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    g_total_bits = SDS_OPTIONS["num_bits"].as< uint32_t >();
    g_num_threads = SDS_OPTIONS["num_threads"].as< uint32_t >();
    g_set_pct = SDS_OPTIONS["set_pct"].as< uint32_t >();
    g_max_bits_in_group = SDS_OPTIONS["set_pct"].as< uint32_t >();

    const auto ret{RUN_ALL_TESTS()};
    return ret;
}
