/*
 * bitset.hpp
 *
 *  Created on: 11-Feb-2017
 *      Author: hkadayam
 */

#pragma once

#include <array>
#include <algorithm>
#include <atomic>
#if __cplusplus > 201703L
#include <bit>
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <sstream>

#include <fmt/format.h>

#include "utility/enum.hpp"

namespace sisl {

static constexpr std::array< uint64_t, 64 > bit_mask{
    static_cast< uint64_t >(1),       static_cast< uint64_t >(1) << 1,  static_cast< uint64_t >(1) << 2,
    static_cast< uint64_t >(1) << 3,  static_cast< uint64_t >(1) << 4,  static_cast< uint64_t >(1) << 5,
    static_cast< uint64_t >(1) << 6,  static_cast< uint64_t >(1) << 7,  static_cast< uint64_t >(1) << 8,
    static_cast< uint64_t >(1) << 9,  static_cast< uint64_t >(1) << 10, static_cast< uint64_t >(1) << 11,
    static_cast< uint64_t >(1) << 12, static_cast< uint64_t >(1) << 13, static_cast< uint64_t >(1) << 14,
    static_cast< uint64_t >(1) << 15, static_cast< uint64_t >(1) << 16, static_cast< uint64_t >(1) << 17,
    static_cast< uint64_t >(1) << 18, static_cast< uint64_t >(1) << 19, static_cast< uint64_t >(1) << 20,
    static_cast< uint64_t >(1) << 21, static_cast< uint64_t >(1) << 22, static_cast< uint64_t >(1) << 23,
    static_cast< uint64_t >(1) << 24, static_cast< uint64_t >(1) << 25, static_cast< uint64_t >(1) << 26,
    static_cast< uint64_t >(1) << 27, static_cast< uint64_t >(1) << 28, static_cast< uint64_t >(1) << 29,
    static_cast< uint64_t >(1) << 30, static_cast< uint64_t >(1) << 31, static_cast< uint64_t >(1) << 32,
    static_cast< uint64_t >(1) << 33, static_cast< uint64_t >(1) << 34, static_cast< uint64_t >(1) << 35,
    static_cast< uint64_t >(1) << 36, static_cast< uint64_t >(1) << 37, static_cast< uint64_t >(1) << 38,
    static_cast< uint64_t >(1) << 39, static_cast< uint64_t >(1) << 40, static_cast< uint64_t >(1) << 41,
    static_cast< uint64_t >(1) << 42, static_cast< uint64_t >(1) << 43, static_cast< uint64_t >(1) << 44,
    static_cast< uint64_t >(1) << 45, static_cast< uint64_t >(1) << 46, static_cast< uint64_t >(1) << 47,
    static_cast< uint64_t >(1) << 48, static_cast< uint64_t >(1) << 49, static_cast< uint64_t >(1) << 50,
    static_cast< uint64_t >(1) << 51, static_cast< uint64_t >(1) << 52, static_cast< uint64_t >(1) << 53,
    static_cast< uint64_t >(1) << 54, static_cast< uint64_t >(1) << 55, static_cast< uint64_t >(1) << 56,
    static_cast< uint64_t >(1) << 57, static_cast< uint64_t >(1) << 58, static_cast< uint64_t >(1) << 59,
    static_cast< uint64_t >(1) << 60, static_cast< uint64_t >(1) << 61, static_cast< uint64_t >(1) << 62,
    static_cast< uint64_t >(1) << 63};

static constexpr std::array< uint64_t, 64 > consecutive_bitmask = {
    (bit_mask[1] - 1),  (bit_mask[2] - 1),  (bit_mask[3] - 1),  (bit_mask[4] - 1),          (bit_mask[5] - 1),
    (bit_mask[6] - 1),  (bit_mask[7] - 1),  (bit_mask[8] - 1),  (bit_mask[9] - 1),          (bit_mask[10] - 1),
    (bit_mask[11] - 1), (bit_mask[12] - 1), (bit_mask[13] - 1), (bit_mask[14] - 1),         (bit_mask[15] - 1),
    (bit_mask[16] - 1), (bit_mask[17] - 1), (bit_mask[18] - 1), (bit_mask[19] - 1),         (bit_mask[20] - 1),
    (bit_mask[21] - 1), (bit_mask[22] - 1), (bit_mask[23] - 1), (bit_mask[24] - 1),         (bit_mask[25] - 1),
    (bit_mask[26] - 1), (bit_mask[27] - 1), (bit_mask[28] - 1), (bit_mask[29] - 1),         (bit_mask[30] - 1),
    (bit_mask[31] - 1), (bit_mask[32] - 1), (bit_mask[33] - 1), (bit_mask[34] - 1),         (bit_mask[35] - 1),
    (bit_mask[36] - 1), (bit_mask[37] - 1), (bit_mask[38] - 1), (bit_mask[39] - 1),         (bit_mask[40] - 1),
    (bit_mask[41] - 1), (bit_mask[42] - 1), (bit_mask[43] - 1), (bit_mask[44] - 1),         (bit_mask[45] - 1),
    (bit_mask[46] - 1), (bit_mask[47] - 1), (bit_mask[48] - 1), (bit_mask[49] - 1),         (bit_mask[50] - 1),
    (bit_mask[51] - 1), (bit_mask[52] - 1), (bit_mask[53] - 1), (bit_mask[54] - 1),         (bit_mask[55] - 1),
    (bit_mask[56] - 1), (bit_mask[57] - 1), (bit_mask[58] - 1), (bit_mask[59] - 1),         (bit_mask[60] - 1),
    (bit_mask[61] - 1), (bit_mask[62] - 1), (bit_mask[63] - 1), ~static_cast< uint64_t >(0)};

template < typename DataType >
static constexpr uint8_t logBase2(const DataType v) {
    static_assert(std::is_unsigned< DataType >::value, "logBase2: DataType must be unsigned.");

    constexpr std::array< uint8_t, 256 > LogTable256{
        255, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5,
        5,   5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6,   6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6,   6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7,   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7,   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7,   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};

    uint8_t r{0};

    if constexpr (sizeof(DataType) == 8) {
        if (const uint64_t t1{v >> 32}) {
            if (const uint64_t t2{t1 >> 16}) {
                if (const uint64_t t3{t2 >> 8}) {
                    r = 56 + LogTable256[static_cast< uint8_t >(t3)];
                } else {
                    r = 48 + LogTable256[static_cast< uint8_t >(t2)];
                }
            } else {
                if (const uint64_t t2{t1 >> 8}) {
                    r = 40 + LogTable256[static_cast< uint8_t >(t2)];
                } else {
                    r = 32 + LogTable256[static_cast< uint8_t >(t1)];
                }
            }
        } else {
            if (const uint64_t t1{v >> 16}) {
                if (const uint64_t t2{t1 >> 8}) {
                    r = 24 + LogTable256[static_cast< uint8_t >(t2)];
                } else {
                    r = 16 + LogTable256[static_cast< uint8_t >(t1)];
                }
            } else {
                if (const uint64_t t1{v >> 8}) {
                    r = 8 + LogTable256[static_cast< uint8_t >(t1)];
                } else {
                    r = LogTable256[static_cast< uint8_t >(v)];
                }
            }
        }
    } else if constexpr (sizeof(DataType) == 4) {
        if (const uint32_t t1{v >> 16}) {
            if (const uint32_t t2{t1 >> 8}) {
                r = 24 + LogTable256[static_cast< uint8_t >(t2)];
            } else {
                r = 16 + LogTable256[static_cast< uint8_t >(t1)];
            }
        } else {
            if (const uint32_t t1{v >> 8}) {
                r = 8 + LogTable256[static_cast< uint8_t >(t1)];
            } else {
                r = LogTable256[static_cast< uint8_t >(v)];
            }
        }
    } else if constexpr (sizeof(DataType) == 2) {
        if (const uint16_t t1{static_cast< uint16_t >(v >> 8)}) {
            r = 8 + LogTable256[static_cast< uint8_t >(t1)];
        } else {
            r = LogTable256[static_cast< uint8_t >(v)];
        }
    } else {
        r = LogTable256[static_cast< uint8_t >(v)];
    }

    return r;
}

#if __cplusplus > 201703L
template < typename DataType >
static inline constexpr uint8_t get_trailing_zeros(const DataType v) {
    return static_cast< uint8_t >(std::countr_zero(std::make_unsigned_t< DataType >(v)));
#else
#if defined __GNUC__ && defined __x86_64
static inline uint8_t get_trailing_zeros(const uint64_t v) {
    return static_cast< uint8_t >((v == 0) ? 64 : __builtin_ctzll(v));
#else
static constexpr uint8_t get_trailing_zeros(const uint64_t v) {
    constexpr std::array< uint8_t, 64 > MultiplyDeBruijnBitPosition{
        0,  47, 1,  56, 48, 27, 2,  60, 57, 49, 41, 37, 28, 16, 3,  61, 54, 58, 35, 52, 50, 42,
        21, 44, 38, 32, 29, 23, 17, 11, 4,  62, 46, 55, 26, 59, 40, 36, 15, 53, 34, 51, 20, 43,
        31, 22, 10, 45, 25, 39, 14, 33, 19, 30, 9,  24, 13, 18, 8,  12, 7,  6,  5,  63};

    if (!v) return 64;
    return MultiplyDeBruijnBitPosition[((v ^ (v - 1)) * static_cast< uint64_t >(0x03F79D71B4CB0A89)) >> 58];
#endif
#endif
}

#if __cplusplus > 201703L
template < typename DataType >
static inline constexpr uint8_t get_set_bit_count(const DataType v) {
    return static_cast< uint8_t >(std::popcount(std::make_unsigned_t< DataType >(v)));
#else
#if defined __GNUC__ && defined __x86_64
static inline uint8_t get_set_bit_count(const uint64_t v) {
    return static_cast< uint8_t >(__builtin_popcountll(v));
#else
static constexpr uint8_t get_set_bit_count(const uint64_t v) {
    constexpr uint64_t m1{0x5555555555555555}; // binary: 0101...
    constexpr uint64_t m2{0x3333333333333333}; // binary: 00110011..
    constexpr uint64_t m4{0x0f0f0f0f0f0f0f0f}; // binary:  4 zeros,  4 ones ...
    // constexpr uint64_t m8{0x00ff00ff00ff00ff};   // binary:  8 zeros,  8 ones ...
    // constexpr uint64_t m16{0x0000ffff0000ffff};  // binary: 16 zeros, 16 ones ...
    // constexpr uint64_t m32{0x00000000ffffffff};  // binary: 32 zeros, 32 ones
    constexpr uint64_t h01{0x0101010101010101}; // the sum of 256 to the power of 0,1,2,3...

    uint64_t x{v};
    x -= (x >> 1) & m1;                             // put count of each 2 bits into those 2 bits
    x = (x & m2) + ((x >> 2) & m2);                 // put count of each 4 bits into those 4 bits
    x = (x + (x >> 4)) & m4;                        // put count of each 8 bits into those 8 bits
    return static_cast< uint8_t >((x * h01) >> 56); // returns left 8 bits of x + (x<<8) + (x<<16) + (x<<24) + ...
#endif
#endif
}

#if __cplusplus > 201703L
template < typename DataType >
static inline constexpr uint8_t get_leading_zeros(const DataType v) {
    return static_cast< uint8_t >(std::countl_zero(std::make_unsigned_t< DataType >(v)));
#else
#if defined __GNUC__ && defined __x86_64
static inline uint8_t get_leading_zeros(const uint64_t v) {
    return static_cast< uint8_t >((v == 0) ? 64 : __builtin_clzll(v));
#else
static inline constexpr uint8_t get_leading_zeros(const uint64_t v) {
    if (!v) return 64;

    uint64_t x{v};
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    x |= (x >> 32);
    return (64 - get_set_bit_count(x));
#endif
#endif
}

ENUM(bit_match_type, uint8_t, no_match, full_match, lsb_match, mid_match, msb_match)

struct bit_filter {
    // All of them are or'd
    uint32_t n_lsb_reqd;
    uint32_t n_mid_reqd;
    uint32_t n_msb_reqd;

    bit_filter(const uint32_t lsb_reqd = 0, const uint32_t mid_reqd = 0, const uint32_t msb_reqd = 0) :
            n_lsb_reqd{lsb_reqd}, n_mid_reqd{mid_reqd}, n_msb_reqd{msb_reqd} {}
    bit_filter(const bit_filter&) = default;
    bit_filter(bit_filter&&) noexcept = default;
    bit_filter& operator=(const bit_filter&) = delete;
    bit_filter& operator=(bit_filter&&) noexcept = delete;
    ~bit_filter() = default;

    std::string to_string() const {
        return fmt::format("n_lsb_reqd={} n_mid_reqd={} n_msb_reqd={} ", n_lsb_reqd, n_mid_reqd, n_msb_reqd);
    }
};

struct bit_match_result {
    bit_match_type match_type;
    uint8_t start_bit;
    uint8_t count;

    bit_match_result(const bit_match_type match_type = bit_match_type::no_match, const uint8_t start_bit = 0,
                     const uint8_t count = 0) :
            match_type{match_type}, start_bit{start_bit}, count{count} {}
    bit_match_result(const bit_match_result&) = default;
    bit_match_result(bit_match_result&&) noexcept = default;
    bit_match_result& operator=(const bit_match_result&) = delete;
    bit_match_result& operator=(bit_match_result&&) noexcept = delete;
    ~bit_match_result() = default;

    std::string to_string() const {

        std::string str{fmt::format("{}", enum_name(match_type))};
        if (match_type != bit_match_type::no_match) {
            fmt::format_to(std::back_inserter(str), " start={} count={}", start_bit, count);
        }
        return str;
    }
};

template < typename Word >
class Bitword {
public:
    static constexpr uint8_t bits() { return (sizeof(Word) * 8); }
    typedef typename Word::word_t word_t;
    static_assert(std::is_unsigned_v< word_t > && (sizeof(word_t) <= 16),
                  "Underlying type must be unsigned of 128 bits or less.");

    Bitword() { m_bits.set(0); }
    explicit Bitword(const Word& b) { m_bits.set(b.get()); }
    explicit Bitword(const word_t& val) { m_bits.set(val); }
    Bitword(const Bitword& other) : m_bits{other.to_integer()} {}
    Bitword(Bitword&&) noexcept = delete;
    Bitword& operator=(const Bitword& rhs) {
        if (this != &rhs) { m_bits.set(rhs.to_integer()); }
        return *this;
    }
    Bitword& operator=(Bitword&&) noexcept = delete;
    ~Bitword() = default;

    void set(const word_t& value) { m_bits.set(value); }

    /**
     * @brief:
     * Total number of bits set in the bitset
     */
    uint8_t get_set_count() const { return get_set_bit_count(m_bits.get()); }

    /**
     * @brief:
     * Total number of bits reset in the bitset
     */
    uint8_t get_reset_count() const { return bits() - get_set_bit_count(m_bits.get()); }

    word_t set_bits(const uint8_t start, const uint8_t nbits) {
        assert(start < bits());
        return set_reset_bits(start, nbits, true /* set */);
    }
    word_t reset_bits(const uint8_t start, const uint8_t nbits) {
        assert(start < bits());
        return set_reset_bits(start, nbits, false /* set */);
    }

    /**
     * @brief: Set or Reset one single bit specified at the start.
     * Params:
     * Start - A bit number which is expected to be less than sizeof(entry_type) * 8
     * set - True if it is to be set or False if reset
     *
     * Returns the final bitmap set of entry_type
     */
    word_t set_reset_bit(const uint8_t start, const bool set) {
        assert(start < bits());
        if (set) {
            return m_bits.or_with(bit_mask[start]);
        } else {
            return m_bits.and_with(~bit_mask[start]);
        }
    }

    /**
     * @brief: Set or Reset multiple bit specified at the start.
     * Params:
     * Start - A bit number which is expected to be less than sizeof(entry_type) * 8
     * nBits - Total number of bits to set from start bit num.
     * set - True if it is to be set or False if reset
     *
     * Returns the final bitmap set of entry_type
     */
    word_t set_reset_bits(const uint8_t start, const uint8_t nbits, const bool set) {
        assert(start < bits());
        if (nbits == 1) { return set_reset_bit(start, set); }

        const uint8_t wanted_bits{std::min< uint8_t >(bits() - start, nbits)};
        const word_t bit_mask{
            static_cast< word_t >(static_cast< word_t >(consecutive_bitmask[wanted_bits - 1]) << start)};
        if (set) {
            return m_bits.or_with(bit_mask);
        } else {
            return m_bits.and_with(~bit_mask);
        }
    }

    bool get_bitval(const uint8_t bit) const { return (m_bits.get() & bit_mask[bit]); }

    /**
     * @brief
     * is_bit_set_reset: Is bits either set or reset from start bit
     */
    bool is_bit_set_reset(const uint8_t start, const bool check_for_set) const {
        assert(start < bits());
        const word_t v{m_bits.get() & static_cast< word_t >(bit_mask[start])};
        return check_for_set ? (v != static_cast< word_t >(0)) : (v == static_cast< word_t >(0));
    }

    bool is_bits_set_reset(const uint8_t start, const uint8_t nbits, const bool check_for_set) const {
        assert(start < bits());
        if (nbits == 1) { return (is_bit_set_reset(start, check_for_set)); }

        const word_t actual{extract(start, nbits)};
        const word_t expected{static_cast< word_t >(check_for_set ? consecutive_bitmask[nbits - 1] : 0)};
        return (actual == expected);
    }

    bool get_next_set_bit(const uint8_t start, uint8_t* const p_set_bit) const {
        assert(start < bits());
        assert(p_set_bit);
        const word_t e{extract(start, bits())};
        if (e) {
            *p_set_bit = get_trailing_zeros(e) + start;
            return true;
        } else {
            return false;
        }
    }

    bool get_next_reset_bit(const uint8_t start, uint8_t* const p_reset_bit) const {
        assert(start < bits());
        assert(p_reset_bit);
        const word_t e{~extract(start, bits())};
        if (e == static_cast< word_t >(0)) {
            return false;
        } else {
            *p_reset_bit = get_trailing_zeros(e) + start;
            return (*p_reset_bit < bits());
        }
    }

    uint8_t get_next_reset_bits(const uint8_t start, uint8_t* const pcount) const {
        assert(start < bits());
        assert(pcount);
        *pcount = 0;
        uint8_t first_0bit{0};
        const word_t e{extract(start, bits())};
        if (e == 0) {
            // Shortcut for all zeros
            first_0bit = start;
            *pcount = bits() - start;
        } else {
            // Find the first 0th bit in the word
            first_0bit = static_cast< uint8_t >(get_trailing_zeros(~e) + start);
            if (first_0bit >= bits()) {
                // No more zero's here in our range.
                first_0bit = bits();
            } else {
                *pcount = std::min< uint8_t >(get_trailing_zeros(e >> (first_0bit - start)), bits() - first_0bit);
            }
        }
        return first_0bit;
    }

    // match the number of bits required at the beginning(lsb), middle(mid), end(msb) of the value
    bit_match_result get_next_reset_bits_filtered(const uint8_t offset, const bit_filter& filter) const {
        assert(offset < bits());
        bit_match_result result{bit_match_type::no_match, offset};
        bool lsb_search{offset == 0};

        word_t e{extract(offset, bits())};
        uint8_t nbits{static_cast< uint8_t >(bits() - offset)}; // Whats the range we are searching for now
        while (nbits > 0) {
            uint8_t first_0bit{get_trailing_zeros(~e)};
            result.start_bit += first_0bit;
            if (first_0bit >= nbits) {
                // No more zero's here in our range.
                result.count = 0;
                break;
            }

            if (first_0bit > 0) {
                // remove all the 1's group
                e = e >> first_0bit;
                nbits -= first_0bit;
            }
            result.count = ((e > static_cast< word_t >(0)) ? get_trailing_zeros(e) : nbits);

            if (lsb_search) {
                if ((first_0bit == static_cast< uint8_t >(0)) && (result.count >= filter.n_lsb_reqd)) {
                    // We matched lsb with required count
                    result.match_type = ((e == 0) ? bit_match_type::full_match : bit_match_type::lsb_match);
                    break;
                }
            }

            if (e == 0) {
                if ((result.count >= filter.n_mid_reqd) || (result.count >= filter.n_msb_reqd)) {
                    result.match_type = bit_match_type::msb_match;
                }
                break;
            } else if (result.count >= filter.n_mid_reqd) {
                result.match_type = bit_match_type::mid_match;
                break;
            }

            e = e >> result.count;
            lsb_search = false;
            nbits -= result.count;
            result.start_bit += result.count;
        }
        return result;
    }

    bool set_next_reset_bit(const uint8_t start, const uint8_t maxbits, uint8_t* const p_bit) {
        assert(start < bits());
        assert(p_bit);
        const bool found{get_next_reset_bit(start, p_bit)};
        if (!found || (*p_bit >= maxbits)) { return false; }

        set_reset_bit(*p_bit, true);
        return true;
    }

    bool set_next_reset_bit(const uint8_t start, uint8_t* const* p_bit) {
        return set_next_reset_bit(start, bits(), p_bit);
    }

    word_t right_shift(const uint8_t nbits) { return m_bits.right_shift(nbits); }

    uint8_t get_max_contiguous_reset_bits(const uint8_t start, uint8_t* const pmax_count) const {
        assert(start < bits());
        assert(pmax_count);

        *pmax_count = 0;
        uint8_t offset{start};
        word_t e{extract(start, bits())};
        uint8_t start_largest_group{std::numeric_limits< uint8_t >::max()};
        while (offset < bits()) {
            if (e == 0) {
                // Shortcut for all zeros
                const uint8_t num_reset_bits{static_cast< uint8_t >(bits() - offset)};
                if (num_reset_bits > *pmax_count) {
                    *pmax_count = num_reset_bits;
                    start_largest_group = offset;
                }
                break;
            } else {
                // Find the first 0th bit in the word
                const uint8_t first_0bit{static_cast< uint8_t >(get_trailing_zeros(~e))};
                if (first_0bit >= bits()) {
                    // No more zero's here in our range.
                    break;
                } else {
                    if (first_0bit > 0) {
                        // remove all the 1's group
                        e = e >> first_0bit;
                        offset += first_0bit;
                    }
                    const uint8_t num_reset_bits{static_cast< uint8_t >(
                        (e > static_cast< word_t >(0)) ? get_trailing_zeros(e) : bits() - offset)};
                    if (num_reset_bits > *pmax_count) {
                        *pmax_count = num_reset_bits;
                        start_largest_group = offset;
                    }
                    // remove the all 0's group
                    offset += num_reset_bits;
                    e >>= num_reset_bits;
                }
            }
        }

        return start_largest_group;
    }

    word_t to_integer() const { return m_bits.get(); }

    std::string to_string() const {
        std::ostringstream oSS{};
        const word_t e{m_bits.get()};
        word_t mask{static_cast< word_t >(bit_mask[bits() - 1])};
        for (uint8_t bit{0}; bit < bits(); ++bit, mask >>= 1) {
            oSS << (((e & mask) == mask) ? '1' : '0');
        }
        return oSS.str();
    }

    void print() const { std::cout << to_string() << std::endl; }

    bool operator==(const Bitword& rhs) const { return m_bits == rhs.m_bits; }

    bool operator!=(const Bitword& rhs) const { return m_bits != rhs.m_bits; }

private:
    word_t extract(const uint8_t start, const uint8_t nbits) const {
        const uint8_t wanted_bits{std::min< uint8_t >(bits() - start, nbits)};
        assert(wanted_bits > 0);
        const word_t mask{static_cast< word_t >(static_cast< word_t >(consecutive_bitmask[wanted_bits - 1]) << start)};
        return ((m_bits.get() & mask) >> start);
    }

private:
    Word m_bits;
};

template < typename charT, typename traits, typename Word >
std::basic_ostream< charT, traits >& operator<<(std::basic_ostream< charT, traits >& outStream,
                                                const Bitword< Word >& bitwordt) {
    // copy the stream formatting
    std::basic_ostringstream< charT, traits > outStringStream;
    outStringStream.copyfmt(outStream);

    // output the date time
    outStringStream << bitwordt.to_string();

    // print the stream
    outStream << outStringStream.str();

    return outStream;
}

template < typename WType >
class unsafe_bits {
public:
    typedef std::decay_t< WType > word_t;
    static_assert(std::is_unsigned_v< word_t >, "Underlying type must be unsigned.");

    unsafe_bits(const word_t& t = static_cast< word_t >(0)) : m_Value{t} {}
    unsafe_bits(const unsafe_bits&) = delete;
    unsafe_bits(unsafe_bits&&) noexcept = delete;
    unsafe_bits& operator=(const unsafe_bits&) = delete;
    unsafe_bits& operator=(unsafe_bits&&) noexcept = delete;
    ~unsafe_bits() = default;

    void set(const word_t& value) { m_Value = value; }
    bool set_if(const word_t& old_value, const word_t& new_value) {
        if (m_Value == old_value) {
            m_Value = new_value;
            return true;
        }
        return false;
    }

    word_t or_with(const word_t value) {
        m_Value |= value;
        return m_Value;
    }

    word_t and_with(const word_t value) {
        m_Value &= value;
        return m_Value;
    }

    word_t right_shift(const uint8_t nbits) {
        m_Value >>= nbits;
        return m_Value;
    }

    word_t get() const { return m_Value; }

    bool operator==(const unsafe_bits& rhs) const { return get() == rhs.get(); }

    bool operator!=(const unsafe_bits& rhs) const { return get() != rhs.get(); }

private:
    word_t m_Value;
};

template < typename WType >
class safe_bits {
public:
    typedef std::decay_t< WType > word_t;
    static_assert(std::is_unsigned_v< word_t >, "Underlying type must be unsigned.");

    safe_bits(const word_t& t = static_cast< word_t >(0)) : m_Value{t} {}
    safe_bits(const safe_bits&) = delete;
    safe_bits(safe_bits&&) noexcept = delete;
    safe_bits& operator=(const safe_bits&) = delete;
    safe_bits& operator=(safe_bits&&) noexcept = delete;
    ~safe_bits() = default;

    void set(const word_t& bits) { m_Value.store(bits, std::memory_order_relaxed); }
    void set_if(const word_t& old_value, const word_t& new_value) {
        word_t expected_value{old_value};
        return m_Value.compare_exchange_strong(expected_value, new_value, std::memory_order_relaxed);
    }

    word_t or_with(const word_t value) {
        const word_t old_value{m_Value.fetch_or(value, std::memory_order_relaxed)};
        return (old_value | value);
    }

    word_t and_with(const word_t value) {
        const word_t old_value{m_Value.fetch_and(value, std::memory_order_relaxed)};
        return (old_value & value);
    }

    word_t right_shift(const uint8_t nbits) {
        word_t old_value{m_Value.get(std::memory_order_acquire)};
        word_t new_value{static_cast< word_t >(old_value >> nbits)};
        while (!m_Value.compare_exchange_weak(old_value, new_value, std::memory_order_acq_rel)) {
            old_value = m_Value.get(std::memory_order_acquire);
            new_value = old_value >> nbits;
        }

        return new_value;
    }
    word_t get() const { return m_Value.load(std::memory_order_relaxed); }

    bool operator==(const safe_bits& rhs) const { return get() == rhs.get(); }

    bool operator!=(const safe_bits& rhs) const { return get() != rhs.get(); }

private:
    std::atomic< word_t > m_Value;
};
} // namespace sisl
