/*
 * bitset.hpp
 *
 *  Created on: 11-Feb-2017
 *      Author: hkadayam
 */

#ifndef CPP_PROXY_SRC_LIBUTILS_FDS_BITSET_HPP_
#define CPP_PROXY_SRC_LIBUTILS_FDS_BITSET_HPP_

#include <assert.h>
#include <atomic>
#include <algorithm>

/*
 * This is a improved bitset, which can efficiently identify and get the leading bitset or reset
 * without walking through every bit. At the moment, the std::bitset or boost::dynamic_bitset are
 * not able to provide such features. Also it provides features like atomically set the next available
 * bits and also will potentially have identify effectively the contiguous bits, max contiguous blocks,
 * atomics for each of these.
 */
namespace fds {
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

typedef uint64_t entry_type;

#define entry_size (sizeof(entry_type) * 8)

class bitset {
  public:
    bitset() { m_bits = 0; }

    bitset(entry_type b) { m_bits = b; }

    /*
     * @brief:
     * Total number of bits set in the bitset
     */
    int get_set_count() {
#ifdef __x86_64
        return __builtin_popcount(m_bits);
#else
        int count = 0;
        entry_type e = m_bits;
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
    int get_reset_count() { return entry_size - get_set_count(); }

    entry_type set_bits(int start, int nbits) { return set_reset_bits(start, nbits, true /* set */); }

    entry_type reset_bits(int start, int nbits) { return set_reset_bits(start, nbits, false /* set */); }

    /* @brief: Set or Reset one single bit specified at the start.
     * Params:
     * Start - A bit number which is expected to be less than sizeof(entry_type) * 8
     * set - True if it is to be set or False if reset
     *
     * Returns the final bitmap set of entry_type
     */
    entry_type set_reset_bit(int start, bool set) {
        if (set) {
            m_bits |= bit_mask[start];
        } else {
            m_bits &= ~bit_mask[start];
        }
        return m_bits;
    }

    /* @brief: Set or Reset multiple bit specified at the start.
     * Params:
     * Start - A bit number which is expected to be less than sizeof(entry_type) * 8
     * nBits - Total number of bits to set from start bit num.
     * set - True if it is to be set or False if reset
     *
     * Returns the final bitmap set of entry_type
     */
    entry_type set_reset_bits(int start, int nbits, bool set) {
        if (nbits == 1) { return set_reset_bit(start, set); }

        int wanted_bits = std::min((int)(entry_size - start), nbits);

        if (set) {
            m_bits |= (consecutive_bitmask[wanted_bits - 1] << start);
        } else {
            m_bits &= (~(consecutive_bitmask[wanted_bits - 1] << start));
        }
        return m_bits;
    }

    /* @brief
     * is_bit_set_reset: Is bits either set or reset from start bit
     */
    bool is_bit_set_reset(int start, bool check_for_set) {
        uint64_t v = m_bits & bit_mask[start];
        return check_for_set ? (v != 0) : (v == 0);
    }

    bool is_bits_set_reset(int start, int nbits, bool check_for_set) {
        if (nbits == 1) { return (is_bit_set_reset(start, check_for_set)); }

        entry_type actual = extract(start, nbits);
        uint64_t expected = check_for_set ? consecutive_bitmask[nbits - 1] : 0;
        return (actual == expected);
    }

    bool get_next_reset_bit(int start, int* p_reset_bit) {
        entry_type e = extract(start, entry_size);

        // Isolated the rightmost 0th bit
        entry_type x = ~e & (e + 1);
        if (x == 0) { return false; }
        *p_reset_bit = logBase2(x) + start;
        return (*p_reset_bit < entry_size);
    }

    bool set_next_reset_bit(int start, int maxbits, int* p_bit) {
        bool found = get_next_reset_bit(start, p_bit);
        if (!found || (*p_bit >= maxbits)) { return false; }

        set_reset_bit(*p_bit, true);
        return true;
    }

    bool set_next_reset_bit(int start, int* p_bit) { return set_next_reset_bit(start, entry_size, p_bit); }

    // TODO: Add max contiguous reset bit and also set_next_reset_bits (multiple version)

    entry_type to_integer() { return m_bits; }

    void print() {
        char str[entry_size + 1] = {0};
        entry_type e = m_bits;

        str[entry_size] = '\0';
        for (int i = entry_size - 1; i >= 0; i--) {
            str[i] = (e & 1) ? '1' : '0';
            e >>= 1;
        }
        printf("%s\n", str);
    }

  private:
    entry_type extract(int start, int nbits) {
        int wanted_bits = std::min((int)(entry_size - start), nbits);
        uint64_t mask = (consecutive_bitmask[wanted_bits - 1] << start);
        return ((m_bits & mask) >> start);
    }

    static int get_trailing_zeros(entry_type e) {
#ifdef __x86_64
        return __builtin_ctzll(e);
#else
        if (e == 0) {
            return entry_size;
        } else {
            return logBase2(e & (-e));
        }
#endif
    }

  private:
    entry_type m_bits;
};

class atomic_bitset {
  public:
    atomic_bitset(int bits) { m_bits = bits; }

    uint32_t get_set_count() { return fds::bitset(m_bits.load()).get_set_count(); }

    void set_bits(int start, int nbits) { set_reset_bits(start, nbits, true /* set */); }

    void reset_bits(int start, int nbits) { set_reset_bits(start, nbits, false /* set */); }

    void set_reset_bits(int start, int nbits, bool set) {
        entry_type oldb;
        entry_type newb;
        do {
            oldb = m_bits.load();
            newb = fds::bitset(oldb).set_reset_bits(start, nbits, set);
        } while (!m_bits.compare_exchange_weak(oldb, newb));
    }

    bool is_bit_set_reset(int start, bool check_for_set) {
        return fds::bitset(m_bits.load()).is_bit_set_reset(start, check_for_set);
    }

    bool is_bits_set_reset(int start, int nbits, bool check_for_set) {
        return fds::bitset(m_bits.load()).is_bits_set_reset(start, nbits, check_for_set);
    }

    int get_next_reset_bit(int start, int* p_reset_start) {
        return fds::bitset(m_bits.load()).get_next_reset_bit(start, p_reset_start);
    }

    /* @brief:
     *
     * bool set_next_reset_bit(int start, int maxbits, int *p_bit)
     *
     * Atomically set the next reset bit available in the trailing side.
     * Parameters:
     * Start - Start bit after which reset bit is sought after.
     * maxbits - Maximum number of bits to look for
     *
     * Output
     * *p_bit: Fills in the bit which was reset. Valid only if return is true.
     *
     * Returns : True if it was able to set one, False if there is not enough reset bits
     * available.
     */
    bool set_next_reset_bit(int start, int maxbits, int* p_bit) {
        uint64_t oldv;
        bitset bset;
        bool done;

        do {
            oldv = m_bits.load();
            bset = fds::bitset(oldv);
            done = bset.set_next_reset_bit(start, maxbits, p_bit);
        } while (!m_bits.compare_exchange_weak(oldv, bset.to_integer()));

        return done;
    }

    bool set_next_reset_bit(int start, int* p_bit) { return set_next_reset_bit(start, entry_size, p_bit); }

    void print() { fds::bitset(m_bits.load()).print(); }

  private:
    std::atomic<entry_type> m_bits;
};
} // namespace fds

#endif /* CPP_PROXY_SRC_LIBUTILS_FDS_BITSET_HPP_ */
