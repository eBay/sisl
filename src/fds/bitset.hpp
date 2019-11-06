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

template < typename Word, bool ThreadSafeResizing = false >
class BitsetImpl {
private:
    mutable folly::SharedMutex m_lock;
    uint64_t m_nbits;
    uint64_t m_skip_bits = 0;

    std::unique_ptr< Word[] > m_words;
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
    explicit BitsetImpl(uint64_t nbits) {
        m_words_cap = ((nbits - 1) / Word::bits() + 1);
        m_words = std::unique_ptr< Word[] >(new Word[m_words_cap]);
        bzero((void*)m_words.get(), m_words_cap * sizeof(Word));
        m_nbits = nbits;
    }

    explicit BitsetImpl(const Word& others) {
        m_words_cap = others.m_words_cap;
        m_words = std::unique_ptr< Word[] >(new Word[m_words_cap]);
        std::memcpy((void*)m_words.get(), (void*)others.m_words.get(), m_words_cap * sizeof(Word));
        m_nbits = others.m_nbits;
        m_skip_bits = others.m_skip_bits;
    }

#ifdef SERIALIZE_SUPPORT
    explicit BitsetImpl(const sisl::blob& b) {
        auto nwords = 0U;

        if (b.size % sizeof(Word) == 0) {
            nwords = b.size / sizeof(Word);
        } else {
            nwords = b.size / sizeof(Word) + 1;
        }

        m_words.reserve(nwords);

        auto p = (Word*)b.bytes;
        for (auto i = 0U; i < nwords; i++) {
            m_words.emplace_back(p[i]);
        }
        m_nbits = nwords * Word::WordType::bits();
    }

    // Serialize the bitset into the blob provided upto blob bytes.
    // Returns if it able completely serialize within the bytes specified.
    bool serialize(const sisl::blob& b) {
        if (b.size < size_serialized()) { return false; }

        if (ThreadSafeResizing) { m_lock.lock(); }
        uint64_t j = 0ULL;
        for (auto& w : m_words) {
            uint64_t t = w.to_integer();
            // for (auto i = 7; i >= 0; i--) {
            for (auto i = 0U; i < 8; i++) {
                // serilize 8 bits once a time
                b.bytes[i + j] = (uint8_t)(t & 0xffULL);
                t >>= 8;
            }
            j += 8; // move on to next word which is 8 bytes;
        }

        if (ThreadSafeResizing) { m_lock.unlock(); }
        return true;
    }

    // Returns how much bytes it will occupy when this bitset is serialized
    // Not the size is rounded up to words, not bits;
    uint32_t size_serialized() const {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }
        auto ret = (sizeof(Word) * m_words.bits());
        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return ret;
    }
#endif

    /**
     * @brief Get total bits available in this bitset
     *
     * @return uint64_t
     */
    uint64_t get_total_bits() const {
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
        m_skip_bits += nbits;
        if (m_skip_bits >= compaction_threshold()) {
            _resize(total_bits());
            /*auto shrink_words = m_skip_bits / Word::bits();
            auto new_skip_bits = m_skip_bits % Word::bits();

            auto new_nbits = nbits + new_skip_bits;
            auto new_cap = (new_nbits - 1) / Word::bits() + 1;
            auto new_cap = m_words_cap - shrink_words;
            auto new_words = std::unique_ptr< Word[] >(new Word[new_cap]);
            std::memmove((void*)new_words.get(), (void*)&(m_words.get()[shrink_words]), (sizeof(Word) * new_cap));

            m_words_cap = new_cap;
            m_words = std::move(new_words);
            m_skip_bits -= (shrink_words * Word::bits()); */
        }

        if (ThreadSafeResizing) { m_lock.unlock(); }
    }

    void resize(uint64_t nbits) {
        if (ThreadSafeResizing) { m_lock.lock(); }
        _resize(nbits);
        if (ThreadSafeResizing) { m_lock.unlock(); }
    }

    // assumption: n < total_bits_in_one_word
    // get contiguous n bits within one word;
    // Limitation: even n is less than Word::bits(), returned bits can not accross two words;
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
        for (Word& w : m_words) {
            w.print();
        }
        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
    }

    std::string to_string() {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }
        std::string out;
        for (Word& w : m_words) {
            out += w.to_string();
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

    void _resize(uint64_t nbits) {
        // We use the resize oppurtunity to compact bits. So we only to need to allocate nbits + first word skip list
        // size. Rest of them will be compacted.
        auto shrink_words = m_skip_bits / Word::bits();
        auto new_skip_bits = m_skip_bits % Word::bits();

        auto new_nbits = nbits + new_skip_bits;
        auto new_cap = (new_nbits - 1) / Word::bits() + 1;
        auto new_words = std::unique_ptr< Word[] >(new Word[new_cap]);
        std::memmove((void*)new_words.get(), (void*)&(m_words.get()[shrink_words]),
                     (sizeof(Word) * std::min(m_words_cap - shrink_words, new_cap)));

        m_words_cap = new_cap;
        m_skip_bits = new_skip_bits;
        m_words = std::move(new_words);
        m_nbits = new_nbits;

        LOGINFO("Resize to total_bits={} total_actual_bits={}, skip_bits={}, words_cap={}", total_bits(), m_nbits,
                m_skip_bits, m_words_cap);
    }

    Word* get_word(uint64_t b) {
        b += m_skip_bits;
        return (sisl_unlikely(b >= m_nbits)) ? nullptr : &m_words[b / Word::bits()];
    }

    const Word* get_word_const(uint64_t b) const {
        b += m_skip_bits;
        return (sisl_unlikely(b >= m_nbits)) ? nullptr : &m_words[b / Word::bits()];
    }

    int get_word_offset(uint64_t b) const {
        b += m_skip_bits;
        return (int)(b % Word::bits());
    }

    uint64_t total_bits() const { return m_nbits - m_skip_bits; }
}; // namespace sisl

typedef BitsetImpl< Bitword< unsafe_bits< uint64_t > >, false > Bitset;
typedef BitsetImpl< Bitword< safe_bits< uint64_t > >, false > AtomicBitset;
typedef BitsetImpl< Bitword< safe_bits< uint64_t > >, true > ThreadSafeBitset;

#if 0
class Bitset : public BitsetImpl< Bitword< unsafe_bits< uint64_t > >, false > {};
class AtomicBitset : public BitsetImpl< Bitword< safe_bits< uint64_t > >, false > {};
class ThreadSafeBitset : public BitsetImpl< Bitword< safe_bits< uint64_t > >, true > {};
#endif

} // namespace sisl
