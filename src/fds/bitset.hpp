/*
 * bitset.hpp
 *
 *  Created on: 11-Feb-2017
 *      Author: hkadayam
 */

#pragma once
#include <cassert>
#include <vector>
#include <algorithm>
#include "bitword.hpp"
#include "utils.hpp"
#include <folly/SharedMutex.h>
#include <sds_logging/logging.h>

/*
 * This is a improved bitset, which can efficiently identify and get the leading bitset or reset
 * without walking through every bit. At the moment, the std::bitset or boost::dynamic_bitset are
 * not able to provide such features. Also it provides features like atomically set the next available
 * bits and also will potentially have identify effectively the contiguous bits, max contiguous blocks,
 * atomics for each of these.
 */

namespace sisl {
struct BitBlock {
    uint64_t start_bit;
    uint32_t nbits;
};

#define call_method(method_name, ...)                                                                                  \
    if (ThreadSafeResizing) {                                                                                          \
        folly::SharedMutexWritePriority::ReadHolder holder(m_lock);                                                    \
        return method_name(__VA_ARGS__);                                                                               \
    } else {                                                                                                           \
        return method_name(__VA_ARGS__);                                                                               \
    }

#if 0
struct bitset_serialized {
    uint64_t nbits;
    uint64_t skip_bits;
    uint64_t words[0];

    static uint64_t nbytes(uint64_t bits) { return sizeof(bitset_serialized) + (total_words(bits) * sizeof(uint64_t)); }
    static uint64_t total_words(uint64_t bits) { return ((bits - 1) / 64) + 1; }
};
#endif

template < typename Word, bool ThreadSafeResizing = false >
class BitsetImpl {
private:
    struct bitset_serialized {
        uint64_t m_id; // persist ID for each bitmap. It is driven by user
        uint64_t m_nbits;
        uint64_t m_skip_bits = 0;
        Word m_words[0];

        static uint64_t nbytes(uint64_t nbits) {
            return sizeof(bitset_serialized) + (total_words(nbits) * sizeof(Word));
        }
        static uint64_t total_words(uint64_t nbits) { return ((nbits - 1) / Word::bits()) + 1; }
    };

    bitset_serialized* m_s = nullptr;
    mutable folly::SharedMutex m_lock;
    sisl::byte_array m_buf;
    uint32_t m_alignment_size = 0;
    uint64_t m_words_cap;

#ifndef NDEBUG
    static constexpr size_t compaction_threshold() { return Word::bits() * 10; }
#else
    // Compact every 1K entries right shifted
    static constexpr size_t compaction_threshold() { return Word::bits() * 1024; }
#endif

public:
    static constexpr uint64_t npos = (uint64_t)-1;

public:
    explicit BitsetImpl(uint64_t nbits, uint64_t m_id = 0, uint32_t alignment_size = 0) {
        m_alignment_size = alignment_size;
        m_buf = sisl::make_byte_array(bitset_serialized::nbytes(nbits), alignment_size);
        m_s = (bitset_serialized*)m_buf->bytes;

        m_s = m_id;
        m_words_cap = bitset_serialized::total_words(nbits);
        // memset((void*)(uint8_t*)(m_s->m_words), 0, (m_words_cap * sizeof(Word) / 2));
        m_s->m_nbits = nbits;
        m_s->m_skip_bits = 0;
        bzero((void*)(uint8_t*)(m_s->m_words), m_words_cap * sizeof(Word));
    }

    explicit BitsetImpl(const BitsetImpl& others) {
        m_alignment_size = others.m_alignment_size;
        m_buf = others.m_buf;
        m_s = (bitset_serialized*)m_buf->bytes;
        m_words_cap = others.m_words_cap;
    }

    explicit BitsetImpl(const sisl::byte_array& b) {
        m_alignment_size = 0; // Assume no alignment
        m_buf = b;
        m_s = (bitset_serialized*)m_buf->bytes;
        m_words_cap = bitset_serialized::total_words(m_s->m_nbits);
    }

    uint64_t get_id() { return m_s->m_id; }

    uint64_t set_id(uint64_t id) { m_s->m_id = id; }

    BitsetImpl& operator=(BitsetImpl&& others) {
        m_alignment_size = others.m_alignment_size;
        m_buf = std::move(others.m_buf);
        m_s = (bitset_serialized*)m_buf->bytes;
        m_words_cap = others.m_words_cap;

        others.m_s = nullptr;
        others.m_words_cap = 0;
        return *this;
    }

    /**
     * @brief Serialize the bitset and return the underlying serialized buffer that can be written as is (which can be
     * used to load later)
     *
     * NOTE: The returned buffer is a const byte array and thus it is expected not to be modified. If modified then it
     * can result in corruption to the bitset.
     *
     * @return sisl::byte_array
     */
    const sisl::byte_array serialize() const {
        if (ThreadSafeResizing) { m_lock.lock(); }
        auto ret = m_buf;
        if (ThreadSafeResizing) { m_lock.unlock(); }
        return ret;
    }

    /**
     * @brief Return the bytes it will have upon serializing
     *
     * @return uint64_t
     */
    uint64_t serialized_size() const {
        if (ThreadSafeResizing) { m_lock.lock(); }
        auto sz = bitset_serialized::nbytes(m_s->m_nbits);
        if (ThreadSafeResizing) { m_lock.unlock(); }
        return sz;
    }

    /**
     * @brief Get total bits available in this bitset
     *
     * @return uint64_t
     */
    uint64_t size() const {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }
        auto ret = total_bits();
        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return ret;
    }

    /**
     * @brief Set the bit. If the bit is outside the available range throws std::out_of_range exception
     *
     * @param b Bit to set
     */
    void set_bit(uint64_t b) { set_reset_bit(b, true); }

    /**
     * @brief Set multiple bits. If the bit is outside the available range throws std::out_of_range exception
     *
     * @param b Starting bit of the sequence to set
     * @param nbits Total number of bits from starting bit
     */
    void set_bits(uint64_t b, int nbits) { set_reset_bits(b, nbits, true); }

    /**
     * @brief Reset the bit. If the bit is outside the available range throws std::out_of_range exception
     *
     * @param b Bit to reset
     */
    void reset_bit(uint64_t b) { set_reset_bit(b, false); }

    /**
     * @brief Reset multiple bits. If the bit is outside the available range throws std::out_of_range exception
     *
     * @param b Starting bit of the sequence to reset
     * @param nbits Total number of bits from starting bit
     */
    void reset_bits(uint64_t b, int nbits) { set_reset_bits(b, nbits, false); }

    /**
     * @brief Is a particular bit is set/reset. If the bit is outside the available range throws std::out_of_range
     * exception
     *
     * @param b Starting bit of the sequence to check
     * @param nbits Total number of bits from starting bit
     */
    bool is_bits_set(uint64_t b, int nbits) const { return is_bits_set_reset(b, nbits, true); }
    bool is_bits_reset(uint64_t b, int nbits) const { return is_bits_set_reset(b, nbits, false); }

    /**
     * @brief Get the value of the bit
     *
     * @param b Bit to get the value of
     * @return true or false based on if bit is set or reset respectively
     */
    bool get_bitval(uint64_t b) const {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }

        const Word* word = get_word_const(b);
        int offset = get_word_offset(b);
        auto ret = word->get_bitval(offset);

        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return ret;
    }

    /**
     * @brief Get the next set bit from given bit
     *
     * @param start_bit Start bit after which (inclusive) search for next bit is on
     * @return uint64_t Returns the next set bit, if one available, else Bitset::npos is returned
     */
    uint64_t get_next_set_bit(uint64_t start_bit) {
        uint64_t ret = npos;
        if (ThreadSafeResizing) { m_lock.lock_shared(); }

        int offset = get_word_offset(start_bit);
        while (true) {
            Word* word = get_word(start_bit);
            if (word == nullptr) { break; }

            // Look for any free bits in the next iteration
            int nbit;
            if (word->get_next_set_bit(offset, &nbit)) {
                ret = start_bit + nbit - offset;
                break;
            }
            start_bit += (Word::bits() - offset);
            offset = 0;
        }

        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return ret;
    }

    /**
     * @brief Right shift the bitset with number of bits provided.
     * NOTE: To be efficient, This method does not immediately right shifts the entire set, rather set the marker and
     * once critical mass (typically 8K right shifts), it actually performs the move of data to right shift.
     *
     * @param nbits Total number of bits to right shift. If it is beyond total number of bits in the bitset, it throws
     * std::out_or_range exception.
     */
    void shrink_head(uint64_t nbits) {
        if (ThreadSafeResizing) { m_lock.lock(); }

        if (nbits > total_bits()) { throw std::out_of_range("Right shift to out of range"); }
        m_s->m_skip_bits += nbits;
        if (m_s->m_skip_bits >= compaction_threshold()) { _resize(total_bits(), false); }

        if (ThreadSafeResizing) { m_lock.unlock(); }
    }

    /**
     * @brief resize the bitset to number of bits. If nbits is more than existing bits, it will expand the bits and set
     * the new bits with value specified in the second parameter. If nbits is less than existing bits, it discards
     * remaining bits.
     *
     * @param nbits: New count of bits the bitset to be reset to
     * @param value: Value to set if bitset is resized up.
     */
    void resize(uint64_t nbits, bool value = false) {
        if (ThreadSafeResizing) { m_lock.lock(); }
        _resize(nbits, value);
        if (ThreadSafeResizing) { m_lock.unlock(); }
    }

    /**
     * @brief Get the next contiguous n reset bits from the start bit
     *
     * @param start_bit Start bit to search from
     * @param n Count of required continous reset bits
     * @return BitBlock Retruns a BitBlock which provides the start bit and total number of bits found. Caller need to
     * check if returned count satisfies what is asked for.
     */
    BitBlock get_next_contiguous_n_reset_bits(uint64_t start_bit, uint32_t n) {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }

        BitBlock retb = {start_bit, 0};
        int n_remaining = (int)n;

        int offset = get_word_offset(start_bit);
        while (n_remaining > 0) {
            Word* word = get_word(start_bit);
            if (word == nullptr) { break; }

            bit_filter filter = {std::min(n_remaining, (int)Word::bits()), (int)n, 1};
            auto result = word->get_next_reset_bits_filtered(offset, filter);

            if (result.match_type == bit_match_type::no_match) {
                // No match, reset everything to what it was before search.
                n_remaining = (int)n;
                retb.start_bit = start_bit + Word::bits() - offset;
            } else if (result.match_type == bit_match_type::mid_match) {
                retb.start_bit = start_bit + result.start_bit - offset;
                retb.nbits = n;
                goto done;
            } else if (result.match_type == bit_match_type::msb_match) {
                // We didn't get what we want, but there are some residue bits, start creating a chain
                n_remaining = (int)n - result.count;
                retb.start_bit = start_bit + result.start_bit - offset;
            } else if (result.match_type == bit_match_type::lsb_match) {
                // We got howmuch ever we need for leading bits. If we have enough to satisy n (note: We might need more
                // than a word width), then respond.
                assert(offset == 0);
                n_remaining -= result.count;
                if (n_remaining <= 0) {
                    retb.nbits = n;
                    goto done;
                }
            }

            start_bit += (Word::bits() - offset);
            offset = 0;
        }

    done:
        if ((retb.start_bit + retb.nbits) > total_bits()) { retb = {npos, 0}; }
        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return retb;
    }

    /**
     * @brief Get the next contiguous reset bits from the start bit upto n bits
     *
     * @param start_bit Start bit to search from
     * @param n Count of required continous reset bits
     * @return BitBlock Retruns a BitBlock which provides the start bit and total number of bits found. Caller need to
     * check if returned count satisfies what is asked for.
     */
    BitBlock get_next_contiguous_upto_n_reset_bits(uint64_t start_bit, uint32_t upto_n) {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }

        int offset = get_word_offset(start_bit);
        BitBlock retb = {0, 0};

        while (1) {
            Bitword64* word = get_word(start_bit);
            if (word == nullptr) { break; }

            // Look for any free bits in the next iteration
            uint32_t nbits;
            retb.start_bit = start_bit + word->get_next_reset_bits(offset, &nbits);
            retb.nbits = nbits;
            if (nbits != 0) { break; }

            start_bit += (Bitword64::size() - offset);
            offset = 0;
        }

        while (retb.nbits < upto_n) {
            if (get_word_offset(retb.start_bit + retb.nbits) != 0) { break; }
            Bitword64* word = get_word(retb.start_bit + retb.nbits);
            uint32_t nbits;
            auto start_bit = word->get_next_reset_bits(0, &nbits);
            if (nbits == 0 || (start_bit != retb.start_bit + retb.nbits)) { break; }
            retb.nbits += nbits;
            if (nbits < Bitword64::size()) { break; }
        }

        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return retb;
    }

    uint64_t get_next_reset_bit(uint64_t start_bit) {
        uint64_t ret = npos;
        if (ThreadSafeResizing) { m_lock.lock_shared(); }

        int offset = get_word_offset(start_bit);
        while (true) {
            Word* word = get_word(start_bit);
            if (word == nullptr) { break; }

            // Look for any free bits in the next iteration

            uint64_t get_next_reset_bit(uint64_t start_bit) {
                uint64_t ret = npos;
                if (ThreadSafeResizing) { m_lock.lock_shared(); }

                int offset = get_word_offset(start_bit);
                while (true) {
                    Word* word = get_word(start_bit);
                    if (word == nullptr) { break; }

                    // Look for any free bits in the next iteration
                    int nbit;
                    if (word->get_next_reset_bit(offset, &nbit)) {
                        ret = start_bit + nbit - offset;
                        if (ret >= total_bits()) ret = npos;
                        break;
                    }
                    start_bit += (Word::bits() - offset);
                    offset = 0;
                }

                if (ThreadSafeResizing) { m_lock.unlock_shared(); }
                return ret;
            }

            void print() {
                if (ThreadSafeResizing) { m_lock.lock_shared(); }
                for (auto i = 0u; i < m_words_cap; ++i) {
                    auto w = nth_word(i);
                    w->print();
                }
                if (ThreadSafeResizing) { m_lock.unlock_shared(); }
            }

            std::string to_string() {
                if (ThreadSafeResizing) { m_lock.lock_shared(); }
                std::string out;
                for (auto i = 0u; i < m_words_cap; ++i) {
                    auto w = nth_word(i);
                    out += w->to_string();
                }
                if (ThreadSafeResizing) { m_lock.unlock_shared(); }
                return out;
            }

        private:
            void set_reset_bits(uint64_t b, int nbits, bool value) {
                if (ThreadSafeResizing) { m_lock.lock_shared(); }

                int offset = get_word_offset(b);
                while (nbits > 0) {
                    Word* word = get_word(b);
                    if (word == nullptr) {
                        throw std::out_of_range("Set/Reset bits not in range");
                        break;
                    }
                    int count = std::min(nbits, (int)Word::bits() - offset);
                    word->set_reset_bits(offset, count, value);

                    b += count;
                    nbits -= count;
                    offset = 0;
                }

                if (ThreadSafeResizing) { m_lock.unlock_shared(); }
            }

            void set_reset_bit(uint64_t b, bool value) {
                if (ThreadSafeResizing) { m_lock.lock_shared(); }

                Word* word = get_word(b);
                int offset = get_word_offset(b);
                word->set_reset_bits(offset, 1, value);

                if (ThreadSafeResizing) { m_lock.unlock_shared(); }
            }

            bool is_bits_set_reset(uint64_t b, int nbits, bool expected) const {
                if (ThreadSafeResizing) { m_lock.lock_shared(); }

                int offset = get_word_offset(b);
                while (nbits > 0) {
                    const Word* word = get_word_const(b);
                    if (word == nullptr) { break; }
                    int count = std::min(nbits, (int)Word::bits() - offset);
                    if (!word->is_bits_set_reset(offset, count, expected)) { return false; }

                    b += count;
                    nbits -= count;
                    offset = 0;
                }

                if (ThreadSafeResizing) { m_lock.unlock_shared(); }
                return true;
            }

            void _resize(uint64_t nbits, bool value) {
                // We use the resize oppurtunity to compact bits. So we only to need to allocate nbits + first word skip
                // list size. Rest of them will be compacted.
                auto shrink_words = m_s->m_skip_bits / Word::bits();
                auto new_skip_bits = m_s->m_skip_bits % Word::bits();

                auto new_nbits = nbits + new_skip_bits;
                auto new_cap = bitset_serialized::total_words(new_nbits);
                auto new_buf = sisl::make_byte_array(bitset_serialized::nbytes(new_nbits), m_alignment_size);
                auto new_s = (bitset_serialized*)new_buf->bytes;

                auto move_nwords = std::min(m_words_cap - shrink_words, new_cap);
                std::memmove((void*)&new_s->m_words[0], (void*)&m_s->m_words[shrink_words],
                             (sizeof(Word) * move_nwords));
                if (new_cap > move_nwords) {
                    // Fill in the remaining space with value passed
                    std::memset((void*)&new_s->m_words[move_nwords], value ? 0xff : 0,
                                (sizeof(Word) * (new_cap - move_nwords)));
                }

                m_words_cap = new_cap;
                m_buf = new_buf;
                m_s = (bitset_serialized*)m_buf->bytes;
                m_s->m_skip_bits = new_skip_bits;
                m_s->m_nbits = new_nbits;

                LOGDEBUG("Resize to total_bits={} total_actual_bits={}, skip_bits={}, words_cap={}", total_bits(),
                         m_s->m_nbits, m_s->m_skip_bits, m_words_cap);
            }

            Word* get_word(uint64_t b) {
                b += m_s->m_skip_bits;
                return (sisl_unlikely(b >= m_s->m_nbits)) ? nullptr : nth_word(b / Word::bits());
            }

            const Word* get_word_const(uint64_t b) const {
                b += m_s->m_skip_bits;
                return (sisl_unlikely(b >= m_s->m_nbits)) ? nullptr : nth_word(b / Word::bits());
            }

            int get_word_offset(uint64_t b) const {
                b += m_s->m_skip_bits;
                return (int)(b % Word::bits());
            }

            uint64_t total_bits() const { return m_s->m_nbits - m_s->m_skip_bits; }
            Word* nth_word(uint64_t word_n) const { return &m_s->m_words[word_n]; }
        };

        /**
         * @brief Bitset: Plain bitset with no safety. Concurrent updates and access are not thread safe and it is
         * expected the user to handle that. This is equivalent to boost::dynamic_bitset
         */
        typedef BitsetImpl< Bitword< unsafe_bits< uint64_t > >, false > Bitset;

        /**
         * @brief AtomicBitset: The only thread safety this version provides is concurrently 2 different bits can be
         * set/unset. However, set/unset concurrently along with increasing the size, setting a bit beyond original
         * size, concurrent test of bits can produce inconsitent values
         *
         * NOTE: It is a very specific, somewhat uncommon use case and hence use it with care. It is typically used
         * where resize and test set bits are all controlled externally.
         */
        typedef BitsetImpl< Bitword< safe_bits< uint64_t > >, false > AtomicBitset;

        /**
         * @brief ThreadSafeBitset: This provides thread safe concurrent set/unset bits and also resize. However, it
         * still can produce inconsistent result if bits are tested concurrently with set/unset bits. Hence one thread
         * doing a set bit and other thread doing a is_bit set for same bit could return inconsistent results. If such
         * requirement exists, use Bitset and take a lock outside the bitset container.
         */
        typedef BitsetImpl< Bitword< safe_bits< uint64_t > >, true > ThreadSafeBitset;

    } // namespace sisl
