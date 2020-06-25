/*
 * bitset.hpp
 *
 *  Created on: 11-Feb-2017
 *      Author: hkadayam
 */

#pragma once
#include <cassert>
#include <atomic>
#include <algorithm>
#include <string.h>
#include <sstream>

namespace sisl {
static uint64_t bit_mask[64] = {
    1ull << 0,  1ull << 1,  1ull << 2,  1ull << 3,  1ull << 4,  1ull << 5,  1ull << 6,  1ull << 7,
    1ull << 8,  1ull << 9,  1ull << 10, 1ull << 11, 1ull << 12, 1ull << 13, 1ull << 14, 1ull << 15,
    1ull << 16, 1ull << 17, 1ull << 18, 1ull << 19, 1ull << 20, 1ull << 21, 1ull << 22, 1ull << 23,
    1ull << 24, 1ull << 25, 1ull << 26, 1ull << 27, 1ull << 28, 1ull << 29, 1ull << 30, 1ull << 31,
    1ull << 32, 1ull << 33, 1ull << 34, 1ull << 35, 1ull << 36, 1ull << 37, 1ull << 38, 1ull << 39,
    1ull << 40, 1ull << 41, 1ull << 42, 1ull << 43, 1ull << 44, 1ull << 45, 1ull << 46, 1ull << 47,
    1ull << 48, 1ull << 49, 1ull << 50, 1ull << 51, 1ull << 52, 1ull << 53, 1ull << 54, 1ull << 55,
    1ull << 56, 1ull << 57, 1ull << 58, 1ull << 59, 1ull << 60, 1ull << 61, 1ull << 62, 1ull << 63};

static uint64_t consecutive_bitmask[64] = {
    ((1ull << 1) - 1),  ((1ull << 2) - 1),  ((1ull << 3) - 1),  ((1ull << 4) - 1),  ((1ull << 5) - 1),
    ((1ull << 6) - 1),  ((1ull << 7) - 1),  ((1ull << 8) - 1),  ((1ull << 9) - 1),  ((1ull << 10) - 1),
    ((1ull << 11) - 1), ((1ull << 12) - 1), ((1ull << 13) - 1), ((1ull << 14) - 1), ((1ull << 15) - 1),
    ((1ull << 16) - 1), ((1ull << 17) - 1), ((1ull << 18) - 1), ((1ull << 19) - 1), ((1ull << 20) - 1),
    ((1ull << 21) - 1), ((1ull << 22) - 1), ((1ull << 23) - 1), ((1ull << 24) - 1), ((1ull << 25) - 1),
    ((1ull << 26) - 1), ((1ull << 27) - 1), ((1ull << 28) - 1), ((1ull << 29) - 1), ((1ull << 30) - 1),
    ((1ull << 31) - 1), ((1ull << 32) - 1), ((1ull << 33) - 1), ((1ull << 34) - 1), ((1ull << 35) - 1),
    ((1ull << 36) - 1), ((1ull << 37) - 1), ((1ull << 38) - 1), ((1ull << 39) - 1), ((1ull << 40) - 1),
    ((1ull << 41) - 1), ((1ull << 42) - 1), ((1ull << 43) - 1), ((1ull << 44) - 1), ((1ull << 45) - 1),
    ((1ull << 46) - 1), ((1ull << 47) - 1), ((1ull << 48) - 1), ((1ull << 49) - 1), ((1ull << 50) - 1),
    ((1ull << 51) - 1), ((1ull << 52) - 1), ((1ull << 53) - 1), ((1ull << 54) - 1), ((1ull << 55) - 1),
    ((1ull << 56) - 1), ((1ull << 57) - 1), ((1ull << 58) - 1), ((1ull << 59) - 1), ((1ull << 60) - 1),
    ((1ull << 61) - 1), ((1ull << 62) - 1), ((1ull << 63) - 1), (uint64_t)-1};

static const char LogTable256[256] = {
#define LT(n) n, n, n, n, n, n, n, n, n, n, n, n, n, n, n, n
    -1,    0,     1,     1,     2,     2,     2,     2,     3,     3,     3,     3,     3,     3,     3,    3,
    LT(4), LT(5), LT(5), LT(6), LT(6), LT(6), LT(6), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7)};

static uint64_t logBase2(uint64_t v) {
    uint64_t r;
    uint64_t t1, t2, t3; // temporaries

    if ((t1 = v >> 32)) {
        if ((t2 = (t1 >> 16))) {
            if ((t3 = (t2 >> 8))) {
                r = 56 + LogTable256[t3];
            } else {
                r = 48 + LogTable256[t2];
            }
        } else {
            if ((t2 = (t1 >> 8))) {
                r = 40 + LogTable256[t2];
            } else {
                r = 32 + LogTable256[t1];
            }
        }
    } else {
        if ((t1 = (v >> 16))) {
            if ((t2 = (t1 >> 8))) {
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

enum class bit_match_type : uint8_t { no_match, lsb_match, mid_match, msb_match };
struct bit_filter {
    // All of them are or'd
    int n_lsb_reqd;
    int n_mid_reqd;
    int n_msb_reqd;

    std::string to_string() const {
        std::stringstream ss;
        ss << "n_lsb_reqd=" << n_lsb_reqd << " n_mid_reqd=" << n_mid_reqd << " n_msb_reqd=" << n_msb_reqd;
        return ss.str();
    }
};

template < typename Word >
class Bitword {
    // class __attribute__((__packed__)) Bitword {
public:
    static constexpr uint32_t bits() { return (sizeof(Word) * 8); }
    typedef typename Word::word_t word_t;

    Bitword() { m_bits.set(0); }

    explicit Bitword(Word b) { m_bits.set(b); }
    explicit Bitword(word_t val) { m_bits.set(val); }

    void set(word_t val) { m_bits.set(val); }

    /*
     * @brief:
     * Total number of bits set in the bitset
     */
    int get_set_count() const {
#ifdef __x86_64
        return __builtin_popcountl(m_bits.get());
#else
        int count = 0;
        word_t e = m_bits.get();
        while (e) {
            e &= (e - 1);
            count++;
        }
        return count;
#endif
    }

    /*
     * @brief:
     * Total number of bits reset in the bitset
     */
    int get_reset_count() const { return bits() - get_set_count(); }

    word_t set_bits(int start, int nbits) { return set_reset_bits(start, nbits, true /* set */); }
    word_t reset_bits(int start, int nbits) { return set_reset_bits(start, nbits, false /* set */); }

    /* @brief: Set or Reset one single bit specified at the start.
     * Params:
     * Start - A bit number which is expected to be less than sizeof(entry_type) * 8
     * set - True if it is to be set or False if reset
     *
     * Returns the final bitmap set of entry_type
     */
    word_t set_reset_bit(int start, bool set) {
        word_t ret;
        if (set) {
            ret = m_bits.or_with(bit_mask[start]);
        } else {
            ret = m_bits.and_with(~bit_mask[start]);
        }
        return ret;
    }

    /* @brief: Set or Reset multiple bit specified at the start.
     * Params:
     * Start - A bit number which is expected to be less than sizeof(entry_type) * 8
     * nBits - Total number of bits to set from start bit num.
     * set - True if it is to be set or False if reset
     *
     * Returns the final bitmap set of entry_type
     */
    word_t set_reset_bits(int start, int nbits, bool set) {
        if (nbits == 1) { return set_reset_bit(start, set); }

        word_t ret;
        int wanted_bits = std::min((int)(bits() - start), nbits);
        if (set) {
            ret = m_bits.or_with(consecutive_bitmask[wanted_bits - 1] << start);
        } else {
            ret = m_bits.and_with(~(consecutive_bitmask[wanted_bits - 1] << start));
        }
        return ret;
    }

    bool get_bitval(int bit) const { return m_bits.get() & bit_mask[bit]; }

    /* @brief
     * is_bit_set_reset: Is bits either set or reset from start bit
     */
    bool is_bit_set_reset(int start, bool check_for_set) const {
        uint64_t v = m_bits.get() & bit_mask[start];
        return check_for_set ? (v != 0) : (v == 0);
    }

    bool is_bits_set_reset(int start, int nbits, bool check_for_set) const {
        if (nbits == 1) { return (is_bit_set_reset(start, check_for_set)); }

        word_t actual = extract(start, nbits);
        uint64_t expected = check_for_set ? consecutive_bitmask[nbits - 1] : 0;
        return (actual == expected);
    }

    bool get_next_set_bit(int start, int* p_set_bit) const {
        word_t e = extract(start, bits());
        if (e) { *p_set_bit = ffsll(e) - 1 + start; }

        //*p_set_bit = (e != 0) && logBase2(e & ~e);
        return (e != 0);
    }

    bool get_next_reset_bit(int start, int* p_reset_bit) const {
        word_t e = extract(start, bits());

        // Isolated the rightmost 0th bit
        word_t x = ~e & (e + 1);
        if (x == 0) { return false; }
        *p_reset_bit = (int)logBase2(x) + start;
        return (*p_reset_bit < (int)bits());
    }

    int get_next_reset_bits(int start, uint32_t* pcount) const {
        uint64_t first_0bit = 0;
        word_t x;

        *pcount = 0;
        word_t e = extract(start, bits());

        if (e == 0) {
            // Shortcut for all zeros
            *pcount = (uint32_t)(bits() - start);
            goto done;
        }

        // Find the first 0th bit in the word
        x = (~e) & (e + 1);
        if (x == 0) {
            // There are no reset bits in the word beyond start
            goto done;
        }
        first_0bit = logBase2(x);
        e >>= first_0bit;

        // Find the first 1st bit in the word
        x = e & (-e);
        if (x == 0) {
            *pcount = (uint32_t)(bits() - start - first_0bit);
            goto done;
        }
        *pcount = (uint32_t)logBase2(x); // next bit number with 1 should be the count of 0s

    done:
        return (uint32_t)first_0bit;
    }

    struct bit_match_result {
        bit_match_type match_type;
        int start_bit;
        int count;

        std::string to_string() const {
            std::stringstream ss;
            ss << "match_type=" << (uint8_t)match_type << " start_bit=" << start_bit << " count=" << count;
            return ss.str();
        }
    };

    bit_match_result get_next_reset_bits_filtered(int offset, const bit_filter& filter) const {
        bit_match_result result;
        int first_0bit = 0;
        bool lsb_search = (offset == 0);

        result.match_type = bit_match_type::no_match;
        result.start_bit = offset;

        word_t e = extract(offset, bits());
        int nbits = bits() - offset; // Whats the range we are searching for now
        while (nbits > 0) {
            result.count = 0;

            first_0bit = ffsll(~e) - 1;
            result.start_bit += first_0bit;
            if ((first_0bit < 0) || (first_0bit > nbits)) {
                // No more zero's here in our range.
                break;
            }

            e = e >> first_0bit;
            nbits -= first_0bit;
            result.count = e ? ffsll(e) - 1 : nbits;

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

    word_t right_shift(int nbits) { return m_bits.right_shift(); }

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

    word_t to_integer() { return m_bits.get(); }

    std::string to_string() {
        char str[bits() + 1] = {0};
        word_t e = m_bits.get();

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
    word_t extract(int start, int nbits) const {
        int wanted_bits = std::min((int)(bits() - start), nbits);
        assert(wanted_bits > 0);
        uint64_t mask = (consecutive_bitmask[wanted_bits - 1] << start);
        return ((m_bits.get() & mask) >> start);
    }

    static int get_trailing_zeros(word_t e) {
#ifdef __x86_64
        return __builtin_ctzll(e);
#else
        if (e == 0) {
            return entry_size();
        } else {
            return logBase2(e & (-e));
        }
#endif
    }

private:
    Word m_bits;
};

template < typename WType >
class unsafe_bits {
    // class __attribute__((__packed__)) unsafe_bits {
public:
    typedef WType word_t;
    unsafe_bits(const WType& t) : m_val(t) {}
    unsafe_bits() : unsafe_bits((WType)0) {}

    void set(WType val) { m_val = val; }
    bool set_if(WType old_val, WType new_val) {
        if (m_val == old_val) {
            m_val = new_val;
            return true;
        }
        return false;
    }

    WType or_with(uint64_t val) {
        m_val |= val;
        return m_val;
    }

    WType and_with(uint64_t val) {
        m_val &= val;
        return m_val;
    }

    WType right_shift(uint32_t nbits) {
        m_val >>= nbits;
        return m_val;
    }

    WType get() const { return m_val; }

private:
    WType m_val;
};

template < typename WType >
class safe_bits {
    // class __attribute__((__packed__)) safe_bits {
public:
    typedef WType word_t;
    safe_bits(const WType& t) : m_val(t) {}
    safe_bits() : safe_bits(0) {}

    void set(WType bits) { m_val.store(bits, std::memory_order_relaxed); }
    void set_if(WType old_val, WType new_val) {
        return m_val.compare_exchange_strong(old_val, new_val, std::memory_order_relaxed);
    }

    WType or_with(uint64_t val) {
        auto old_val = m_val.fetch_or(val, std::memory_order_relaxed);
        return (old_val |= val);
    }

    WType and_with(uint64_t val) {
        auto old_val = m_val.fetch_and(val, std::memory_order_relaxed);
        return (old_val &= val);
    }

    WType right_shift(uint32_t nbits) {
        WType old_val;
        WType new_val;
        do {
            old_val = m_val.get(std::memory_order_acquire);
            new_val = old_val >> nbits;
        } while (!m_val.compare_exchange_weak(old_val, new_val, std::memory_order_acq_rel));

        return new_val;
    }
    WType get() const { return m_val.load(std::memory_order_relaxed); }

private:
    std::atomic< WType > m_val;
};
} // namespace sisl
