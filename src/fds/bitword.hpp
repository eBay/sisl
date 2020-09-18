/*
 * bitset.hpp
 *
 *  Created on: 11-Feb-2017
 *      Author: hkadayam
 */

#pragma once

#include <array>
#include <atomic>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <sstream>

namespace sisl {

static constexpr std::array<uint64_t, 64> bit_mask {
    static_cast<uint64_t>(1),  static_cast<uint64_t>(1) << 1,
    static_cast<uint64_t>(1) << 2,  static_cast<uint64_t>(1) << 3,
    static_cast<uint64_t>(1) << 4,  static_cast<uint64_t>(1) << 5,
    static_cast<uint64_t>(1) << 6,  static_cast<uint64_t>(1) << 7,
    static_cast<uint64_t>(1) << 8,  static_cast<uint64_t>(1) << 9,
    static_cast<uint64_t>(1) << 10, static_cast<uint64_t>(1) << 11,
    static_cast<uint64_t>(1) << 12, static_cast<uint64_t>(1) << 13,
    static_cast<uint64_t>(1) << 14, static_cast<uint64_t>(1) << 15,
    static_cast<uint64_t>(1) << 16, static_cast<uint64_t>(1) << 17,
    static_cast<uint64_t>(1) << 18, static_cast<uint64_t>(1) << 19,
    static_cast<uint64_t>(1) << 20, static_cast<uint64_t>(1) << 21,
    static_cast<uint64_t>(1) << 22, static_cast<uint64_t>(1) << 23,
    static_cast<uint64_t>(1) << 24, static_cast<uint64_t>(1) << 25,
    static_cast<uint64_t>(1) << 26, static_cast<uint64_t>(1) << 27,
    static_cast<uint64_t>(1) << 28, static_cast<uint64_t>(1) << 29,
    static_cast<uint64_t>(1) << 30, static_cast<uint64_t>(1) << 31,
    static_cast<uint64_t>(1) << 32, static_cast<uint64_t>(1) << 33,
    static_cast<uint64_t>(1) << 34, static_cast<uint64_t>(1) << 35,
    static_cast<uint64_t>(1) << 36, static_cast<uint64_t>(1) << 37,
    static_cast<uint64_t>(1) << 38, static_cast<uint64_t>(1) << 39,
    static_cast<uint64_t>(1) << 40, static_cast<uint64_t>(1) << 41,
    static_cast<uint64_t>(1) << 42, static_cast<uint64_t>(1) << 43,
    static_cast<uint64_t>(1) << 44, static_cast<uint64_t>(1) << 45,
    static_cast<uint64_t>(1) << 46, static_cast<uint64_t>(1) << 47,
    static_cast<uint64_t>(1) << 48, static_cast<uint64_t>(1) << 49,
    static_cast<uint64_t>(1) << 50, static_cast<uint64_t>(1) << 51,
    static_cast<uint64_t>(1) << 52, static_cast<uint64_t>(1) << 53,
    static_cast<uint64_t>(1) << 54, static_cast<uint64_t>(1) << 55,
    static_cast<uint64_t>(1) << 56, static_cast<uint64_t>(1) << 57,
    static_cast<uint64_t>(1) << 58, static_cast<uint64_t>(1) << 59,
    static_cast<uint64_t>(1) << 60, static_cast<uint64_t>(1) << 61,
    static_cast<uint64_t>(1) << 62, static_cast<uint64_t>(1) << 63};

static constexpr std::array<uint64_t,64> consecutive_bitmask = {
    (bit_mask[1] - 1), (bit_mask[2] - 1), (bit_mask[3] - 1),
    (bit_mask[4] - 1), (bit_mask[5] - 1), (bit_mask[6] - 1),
    (bit_mask[7] - 1), (bit_mask[8] - 1), (bit_mask[9] - 1),
    (bit_mask[10] - 1), (bit_mask[11] - 1), (bit_mask[12] - 1),
    (bit_mask[13] - 1), (bit_mask[14] - 1), (bit_mask[15] - 1),
    (bit_mask[16] - 1), (bit_mask[17] - 1), (bit_mask[18] - 1),
    (bit_mask[19] - 1), (bit_mask[20] - 1), (bit_mask[21] - 1),
    (bit_mask[22] - 1), (bit_mask[23] - 1), (bit_mask[24] - 1),
    (bit_mask[25] - 1), (bit_mask[26] - 1), (bit_mask[27] - 1),
    (bit_mask[28] - 1), (bit_mask[29] - 1), (bit_mask[30] - 1),
    (bit_mask[31] - 1), (bit_mask[32] - 1), (bit_mask[33] - 1),
    (bit_mask[34] - 1), (bit_mask[35] - 1), (bit_mask[36] - 1),
    (bit_mask[37] - 1), (bit_mask[38] - 1), (bit_mask[39] - 1),
    (bit_mask[40] - 1), (bit_mask[41] - 1), (bit_mask[42] - 1),
    (bit_mask[43] - 1), (bit_mask[44] - 1), (bit_mask[45] - 1),
    (bit_mask[46] - 1), (bit_mask[47] - 1), (bit_mask[48] - 1),
    (bit_mask[49] - 1), (bit_mask[50] - 1), (bit_mask[51] - 1),
    (bit_mask[52] - 1), (bit_mask[53] - 1), (bit_mask[54] - 1),
    (bit_mask[55] - 1), (bit_mask[56] - 1), (bit_mask[57] - 1),
    (bit_mask[58] - 1), (bit_mask[59] - 1), (bit_mask[60] - 1),
    (bit_mask[61] - 1), (bit_mask[62] - 1), (bit_mask[63] - 1),
    ~static_cast<uint64_t>(0)};


static constexpr uint8_t logBase2(const uint64_t v) {
    constexpr std::array< uint8_t, 256 > LogTable256{
        255, 0, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 3, 3, 3, 3,
        4, 4, 4, 4, 4, 4, 4, 4,
        4, 4, 4, 4, 4, 4, 4, 4,
        5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5,
        6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7};

    uint8_t r{0};

    if (uint64_t t1{v >> 32}) {
        if (const uint64_t t2{t1 >> 16}) {
            if (const uint64_t t3{t2 >> 8}) {
                r = 56 + LogTable256[t3];
            } else {
                r = 48 + LogTable256[t2];
            }
        } else {
            if (const uint64_t t2{t1 >> 8}) {
                r = 40 + LogTable256[t2];
            } else {
                r = 32 + LogTable256[t1];
            }
        }
    } else {
        if ((t1 = (v >> 16))) {
            if (const uint64_t t2{t1 >> 8}) {
                r = 24 + LogTable256[t2];
            } else {
                r = 16 + LogTable256[t1];
            }
        } else {
            if ((t1 = (v >> 8))) {
                r = 8 + LogTable256[t1];
            } else {
                r = LogTable256[v];
            }
        }
    }

    return r;
}

 static constexpr uint8_t get_trailing_zeros(const uint64_t v) {
    constexpr std::array< uint8_t, 64 > MultiplyDeBruijnBitPosition{
        0,  47, 1,  56, 48, 27, 2,  60, 57,
        49, 41, 37, 28, 16, 3,  61, 54, 58,
        35, 52, 50, 42, 21, 44, 38, 32, 29,
        23, 17, 11, 4,  62, 46, 55, 26, 59,
        40, 36, 15, 53, 34, 51, 20, 43, 31,
        22, 10, 45, 25, 39, 14, 33, 19, 30,
        9,  24, 13, 18, 8,  12, 7,  6,  5,
        63};

    if (!v) return 64;
    return MultiplyDeBruijnBitPosition[((v ^ (v-1)) * static_cast<uint64_t>(0x03F79D71B4CB0A89)) >> 58];
}

enum class bit_match_type : uint8_t { no_match, lsb_match, mid_match, msb_match };

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

    std::string to_string() const {
        std::ostringstream ss{};
        ss << "n_lsb_reqd=" << n_lsb_reqd << " n_mid_reqd=" << n_mid_reqd << " n_msb_reqd=" << n_msb_reqd;
        return ss.str();
    }
};
template < typename Word >
class Bitword {
    // class __attribute__((__packed__)) Bitword {
public:
    static constexpr uint8_t bits() { return (sizeof(Word) * 8); }
    typedef typename Word::word_t word_t;

    Bitword() { m_Bits.set(0); }
    explicit Bitword(const Word& b) { m_Bits.set(b); }
    explicit Bitword(const word_t& val) { m_Bits.set(val); }
    Bitword(const Bitword&) = delete;
    Bitword(Bitword&&) noexcept  = delete;
    Bitword& operator=(const Bitword&) = delete;
    Bitword& operator=(Bitword&&) noexcept = delete;

    void set(const word_t& value) { m_Bits.set(value); }

    //
    // @brief:
    // Total number of bits set in the bitset
    //
    uint8_t get_set_count() const {
//#ifdef __x86_64
        //return __builtin_popcountl(m_Bits.get());
//#else
    static constexpr uint64_t m1{0x5555555555555555};   // binary: 0101...
    static constexpr uint64_t m2{0x3333333333333333};   // binary: 00110011..
    static constexpr uint64_t m4{0x0f0f0f0f0f0f0f0f};   // binary:  4 zeros,  4 ones ...
    //static constexpr uint64_t m8{0x00ff00ff00ff00ff};   // binary:  8 zeros,  8 ones ...
    //static constexpr uint64_t m16{0x0000ffff0000ffff};  // binary: 16 zeros, 16 ones ...
    //static constexpr uint64_t m32{0x00000000ffffffff};  // binary: 32 zeros, 32 ones
    static constexpr uint64_t h01{0x0101010101010101};  // the sum of 256 to the power of 0,1,2,3...

    uint64_t x{m_Bits.get()};
    x -= (x >> 1) & m1;    ;         // put count of each 2 bits into those 2 bits
    x = (x & m2) + ((x >> 2) & m2); // put count of each 4 bits into those 4 bits
    x = (x + (x >> 4)) & m4;        // put count of each 8 bits into those 8 bits
    return static_cast<uint8_t>((x * h01) >> 56);  // returns left 8 bits of x + (x<<8) + (x<<16) + (x<<24) + ...
//#endif
    }

    //
    // @brief:
    // Total number of bits reset in the bitset
    //
    uint8_t get_reset_count() const { return bits() - get_set_count(); }

    word_t set_bits(const uint8_t start, const uint8_t nbits) { return set_reset_bits(start, nbits, true /* set */); }
    word_t reset_bits(const uint8_t start, const uint8_t nbits) {
        return set_reset_bits(start, nbits, false /* set */);
    }

    // @brief: Set or Reset one single bit specified at the start.
    // Params:
    // Start - A bit number which is expected to be less than sizeof(entry_type) * 8
    // set - True if it is to be set or False if reset
    //
    // Returns the final bitmap set of entry_type
    //
    word_t set_reset_bit(const uint8_t start, const bool set) {
        if (set) {
            return m_Bits.or_with(bit_mask[start]);
        } else {
            return m_Bits.and_with(~bit_mask[start]);
        }
    }

    // @brief: Set or Reset multiple bit specified at the start.
    // Params:
    // Start - A bit number which is expected to be less than sizeof(entry_type) * 8
    // nBits - Total number of bits to set from start bit num.
    // set - True if it is to be set or False if reset
    //
    // Returns the final bitmap set of entry_type
    //
    word_t set_reset_bits(const uint8_t start, const uint8_t nbits, const bool set) {
        if (nbits == 1) { return set_reset_bit(start, set); }

        const uint8_t wanted_bits{std::min<uint8_t>(bits() - start, nbits)};
        const uint64_t bit_mask{consecutive_bitmask[wanted_bits - 1] << start};
        if (set) {
            return m_Bits.or_with(bit_mask);
        } else {
            return m_Bits.and_with(~bit_mask);
        }
    }

    bool get_bitval(const uint8_t bit) const { return m_Bits.get() & bit_mask[bit]; }

    // @brief
    // is_bit_set_reset: Is bits either set or reset from start bit
    //
    bool is_bit_set_reset(const uint8_t start, const bool check_for_set) const {
        const uint64_t v{m_Bits.get() & bit_mask[start]};
        return check_for_set ? (v != 0) : (v == 0);
    }

    bool is_bits_set_reset(const uint8_t start, const uint8_t nbits, const bool check_for_set) const {
        if (nbits == 1) { return (is_bit_set_reset(start, check_for_set)); }

        const word_t actual{extract(start, nbits)};
        const uint64_t expected{check_for_set ? consecutive_bitmask[nbits - 1] : 0};
        return (actual == expected);
    }

    bool get_next_set_bit(const uint8_t start, uint8_t* const p_set_bit) const {
        const word_t e{extract(start, bits())};
        if (e) { *p_set_bit = get_trailing_zeros(e) + start; }
        //if (e) { *p_set_bit = ffsll(e) - 1 + start; }
        //*p_set_bit = (e != 0) && logBase2(e & ~e);
        return (e != 0);
    }

    bool get_next_reset_bit(const uint8_t start, uint8_t* const p_reset_bit) const {
        const word_t e{extract(start, bits())};

        // Isolated the rightmost 0th bit
        const word_t x{~e & (e + 1)};
        if (x == 0) { return false; }
        *p_reset_bit = logBase2(x) + start;
        return (*p_reset_bit < bits());
    }

    uint8_t get_next_reset_bits(const uint8_t start, uint8_t* const pcount) const {
        *pcount = 0;
        uint8_t first_0bit{0};
        const word_t e{extract(start, bits())};

        if (e == 0) {
            // Shortcut for all zeros
            *pcount = bits() - start;
        } else {
            // Find the first 0th bit in the word
            word_t x{(~e) & (e + 1)};
            if (x == 0) {
                // There are no reset bits in the word beyond start
            } else {
                first_0bit = logBase2(x);
                e >>= first_0bit;

                // Find the first 1st bit in the word
                x = e & (-e);
                if (x == 0) {
                    *pcount = (bits() - start - first_0bit);
                } else {
                    *pcount = logBase2(x); // next bit number with 1 should be the count of 0s
                }
            }
        }
        return first_0bit;
    }

    struct bit_match_result {
        bit_match_type match_type;
        uint8_t start_bit;
        uint8_t count;

        bit_match_result(const bit_match_type match_type, const uint8_t start_bit, const uint8_t count = 0) :
                match_type{match_type}, start_bit{start_bit}, count{count} {}
        bit_match_result(const bit_match_result&) = default;
        bit_match_result(bit_match_result&&) noexcept = default;
        bit_match_result& operator=(const bit_match_result&) = delete;
        bit_match_result& operator=(bit_match_result&&) noexcept = delete;

        std::string to_string() const {
            std::ostringstream ss;
            ss << "match_type=" << static_cast< uint16_t >(match_type)
               << " start_bit=" << static_cast< uint16_t >(start_bit) << " count=" << static_cast< uint16_t >(count);
            return ss.str();
        }
    };

    bit_match_result get_next_reset_bits_filtered(const uint8_t offset, const bit_filter& filter) const {
        bit_match_result result{bit_match_type::no_match, offset};
        bool lsb_search{offset == 0};

        word_t e{extract(offset, bits())};
        uint8_t nbits{static_cast<uint8_t>(bits() - offset)}; // Whats the range we are searching for now
        while (nbits > 0) {
            result.count = 0;
            uint8_t first_0bit{get_trailing_zeros(~e)};
            result.start_bit += first_0bit;
            if (first_0bit >= nbits) {
                // No more zero's here in our range.
                break;
            }

            e = e >> first_0bit;
            nbits -= first_0bit;
            result.count = e ? get_trailing_zeros(e) : nbits;

            if (lsb_search) {
                if ((first_0bit == 0) && (result.count >= filter.n_lsb_reqd)) {
                    // We matched lsb with required count
                    result.match_type = bit_match_type::lsb_match;
                    break;
                }
                lsb_search = false;
            }

            if (result.count >= filter.n_mid_reqd) {
                result.match_type = bit_match_type::mid_match;
                break;
            }

            // Not enought count for lsb and mid match, keep going
            e = e >> result.count;
            if (e == 0) { break; }

            nbits -= result.count;
            result.start_bit += result.count;
        }

        if ((result.match_type == bit_match_type::no_match) && (result.count >= filter.n_msb_reqd)) {
            result.match_type = bit_match_type::msb_match;
        }
        return result;
    }

    bool set_next_reset_bit(int start, int maxbits, int* p_bit) {
        bool found = get_next_reset_bit(start, p_bit);
        if (!found || (*p_bit >= maxbits)) { return false; }

        set_reset_bit(*p_bit, true);
        return true;
    }

    bool set_next_reset_bit(int start, int* p_bit) { return set_next_reset_bit(start, bits(), p_bit); }

    word_t right_shift(int nbits) { return m_Bits.right_shift(); }

    int get_max_contigous_reset_bits(int start, uint32_t* pmax_count) const {
        uint64_t cur_bit = 0;
        uint64_t prev_bit = 0;
        int ret_bit = -1;
        uint64_t count;
        *pmax_count = 0;

        if (start == bits()) { return -1; }

        word_t e = extract(start, bits());
        while ((e != 0)) {
            uint64_t x = e & (-e);
            e &= ~x;
            cur_bit = logBase2(x);
            count = cur_bit - prev_bit;

            if (count > *pmax_count) {
                ret_bit = (int)prev_bit + start;
                *pmax_count = (uint32_t)count;
            }
            prev_bit = cur_bit + 1;
        }

        // Find the leading 0s
        count = bits() - start - prev_bit;
        if (count > *pmax_count) {
            ret_bit = (int)prev_bit + start;
            *pmax_count = (uint32_t)count;
        }

        return ret_bit;
    }

    word_t to_integer() { return m_Bits.get(); }

    std::string to_string() {
        char str[bits() + 1] = {0};
        word_t e = m_Bits.get();

        str[bits()] = '\0';
        for (int i = bits() - 1; i >= 0; i--) {
            str[i] = (e & 1) ? '1' : '0';
            e >>= 1;
        }
        return std::string(str);
    }

    void print() {
        std::string str = to_string();
        printf("%s\n", str.c_str());
    }

private:
    word_t extract(const u_int8_t start, const uint8_t nbits) const {
        const uint8_t wanted_bits{std::min<uint8_t>(bits() - start, nbits)};
        assert(wanted_bits > 0);
        const uint64_t mask{(consecutive_bitmask[wanted_bits - 1] << start)};
        return ((m_Bits.get() & mask) >> start);
    }

private:
    Word m_Bits;
};

template < typename WType >
class unsafe_bits {
    // class __attribute__((__packed__)) unsafe_bits {
public:
    typedef WType word_t;

    unsafe_bits(const WType& t = static_cast< WType >(0)) : m_Value(t) {}
    unsafe_bits(const unsafe_bits&) = delete;
    unsafe_bits(unsafe_bits&&) noexcept = delete;
    unsafe_bits& operator=(const unsafe_bits&) = delete;
    unsafe_bits& operator=(unsafe_bits&&) noexcept = delete;

    void set(const WType& value) { m_Value = value; }
    bool set_if(const WType& old_value, const WType& new_value) {
        if (m_Value == old_value) {
            m_Value = new_value;
            return true;
        }
        return false;
    }

    WType or_with(const uint64_t value) {
        m_Value |= value;
        return m_Value;
    }

    WType and_with(const uint64_t value) {
        m_Value &= value;
        return m_Value;
    }

    WType right_shift(const uint8_t nbits) {
        m_Value >>= nbits;
        return m_Value;
    }

    WType get() const { return m_Value; }

private:
    WType m_Value;
};

template < typename WType >
class safe_bits {
    // class __attribute__((__packed__)) safe_bits {
public:
    typedef WType word_t;

    safe_bits(const WType& t = static_cast<WType>(0)) : m_Value{t} {}
    safe_bits(const safe_bits&) = delete;
    safe_bits(safe_bits&&) noexcept = delete;
    safe_bits& operator=(const safe_bits&) = delete;
    safe_bits& operator=(safe_bits&&) noexcept = delete;

    void set(const WType& bits) { m_Value.store(bits, std::memory_order_relaxed); }
    void set_if(const WType& old_value, const WType& new_value) {
        return m_Value.compare_exchange_strong(old_value, new_value, std::memory_order_relaxed);
    }

    WType or_with(const uint64_t value) {
        const auto old_value{m_Value.fetch_or(value, std::memory_order_relaxed)};
        return (old_value | value);
    }

    WType and_with(const uint64_t value) {
        const auto old_value{m_Value.fetch_and(value, std::memory_order_relaxed)};
        return (old_value & value);
    }

    WType right_shift(const uint8_t nbits) {
        WType old_value{m_Value.get(std::memory_order_acquire)};
        WType new_value{old_value >> nbits};
        while (!m_Value.compare_exchange_weak(old_value, new_value, std::memory_order_acq_rel)) {
            old_value = m_Value.get(std::memory_order_acquire);
            new_value = old_value >> nbits;
        };

        return new_value;
    }
    WType get() const { return m_Value.load(std::memory_order_relaxed); }

private:
    std::atomic< WType > m_Value;
};
} // namespace sisl
