/*
 * bitset.hpp
 *
 *  Created on: 11-Feb-2017
 *      Author: hkadayam
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <folly/SharedMutex.h>
#include <sds_logging/logging.h>

#include "bitword.hpp"
#include "utils.hpp"


//
// This is a improved bitset, which can efficiently identify and get the leading bitset or reset
// without walking through every bit. At the moment, the std::bitset or boost::dynamic_bitset are
// not able to provide such features. Also it provides features like atomically set the next available
// bits and also will potentially have identify effectively the contiguous bits, max contiguous blocks,
// atomics for each of these.
//

namespace sisl {
struct BitBlock {
    uint64_t start_bit;
    uint32_t nbits;
    BitBlock(const uint64_t start, const uint32_t bits) : start_bit{start}, nbits{bits} {}
    BitBlock(const BitBlock&) = default;
    BitBlock(BitBlock&&) noexcept = default;
    BitBlock& operator=(const BitBlock&) = default;
    BitBlock& operator=(BitBlock&&) noexcept = default;
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
    static_assert(Word::bits() == 8 || Word::bits() == 16 || Word::bits() == 32 ||Word::bits() == 64,
                  "Word::bits() must be power of two in size");

private:
#pragma pack(1)
    struct bitset_serialized {
        uint64_t m_id; // persist ID for each bitmap. It is driven by user
        uint64_t m_nbits;
        uint64_t m_skip_bits;
        Word m_words[1];

        bitset_serialized(const uint64_t id, const uint64_t nbits, const uint64_t skip_bits = 0) :
                m_id{id},
                m_nbits{nbits},
                m_skip_bits{skip_bits} {
            ::memset(static_cast< void* >(m_words), 0, total_words(nbits) * sizeof(Word));
        }
        bitset_serialized(const bitset_serialized&) = delete;
        bitset_serialized(bitset_serialized&&) noexcept = delete;
        bitset_serialized& operator=(const bitset_serialized&) = delete;
        bitset_serialized& operator=(bitset_serialized&&) noexcept = delete;

        bool valid_bit(const uint64_t bit) const { return bit + m_skip_bits < m_nbits; }
        const Word* end_words() const { return m_words + total_words(m_nbits); }
        static constexpr uint64_t nbytes(const uint64_t nbits) {
            return sizeof(bitset_serialized) + (total_words(nbits) - 1) * sizeof(Word);
        }
        static constexpr uint64_t total_words(const uint64_t nbits)
        {
            return ((nbits / Word::bits()) + (((nbits & m_word_mask) > 0) ? 1 : 0));
        }
    };
#pragma pack()

    uint32_t m_alignment_size{0};
    sisl::byte_array m_buf;
    bitset_serialized* m_s{nullptr};
    uint64_t m_words_cap;
    mutable folly::SharedMutex m_lock;
    static constexpr uint64_t m_word_mask{Word::bits() - 1};

#ifndef NDEBUG
    static constexpr size_t compaction_threshold() { return Word::bits() * 10; }
#else
    // Compact every 1K entries right shifted
    static constexpr size_t compaction_threshold() { return Word::bits() * 1024; }
#endif

public:
    static constexpr uint64_t npos{std::numeric_limits< uint64_t >::max()};

public:
    explicit BitsetImpl(const uint64_t nbits, const uint64_t m_id = 0, const uint32_t alignment_size = 0) :
        m_alignment_size{alignment_size}
    {
        const uint64_t size{alignment_size ? round_up(bitset_serialized::nbytes(nbits), alignment_size)
                                           : bitset_serialized::nbytes(nbits)};
        m_buf = make_byte_array(size, alignment_size);
        m_s = new (m_buf->bytes) bitset_serialized{m_id, nbits};
        m_words_cap = bitset_serialized::total_words(nbits);
    }

    explicit BitsetImpl(const BitsetImpl& others) :
        m_alignment_size{others.m_alignment_size},
        m_buf{others.m_buf},
        m_s{reinterpret_cast< bitset_serialized* >(m_buf->bytes)},
        m_words_cap{others.m_words_cap} {
    }

    explicit BitsetImpl(const sisl::byte_array& b) {
        // NOTE: This assumes that the passed byte_array already has an initialized bitset_serialized structure
        m_alignment_size = 0; // Assume no alignment
        m_buf = b;
        m_s = reinterpret_cast< bitset_serialized* >(m_buf->bytes);
        m_words_cap = bitset_serialized::total_words(m_s->m_nbits);
    }

    BitsetImpl(BitsetImpl&& others) noexcept : m_alignment_size{std::move(others.m_alignment_size)},
        m_buf{std::move(others.m_buf)},
        m_s{reinterpret_cast< bitset_serialized* >(m_buf->bytes)},
        m_words_cap{std::move(others.m_words_cap)} {
        others.m_alignment_size = 0;
        others.m_s = nullptr;
        others.m_words_cap = 0;
    }

    BitsetImpl& operator=(const BitsetImpl& rhs)
    {
        if (this != &rhs) {
            m_alignment_size = rhs.m_alignment_size;
            m_buf = rhs.m_buf;
            m_s = reinterpret_cast< bitset_serialized* >(m_buf->bytes);
            m_words_cap = rhs.m_words_cap;
        }
        return *this;
    }

    BitsetImpl& operator=(BitsetImpl&& rhs) noexcept
    {
        if (this != &rhs)
        {
            m_alignment_size = std::move(rhs.m_alignment_size);
            m_buf = std::move(rhs.m_buf);
            m_s = reinterpret_cast< bitset_serialized* >(m_buf->bytes);
            m_words_cap = std::move(rhs.m_words_cap);

            rhs.m_alignment_size = 0;
            rhs.m_s = nullptr;
            rhs.m_words_cap = 0;
        }
        return *this;
    }

    uint64_t get_id() const { return m_s->m_id; }

    void set_id(uint64_t id) { m_s->m_id = id; }

    void copy(const BitsetImpl& others) {
        if (!m_buf || m_buf->size != others.m_buf->size) {
            m_buf = make_byte_array(others.m_buf->size, others.m_alignment_size);
            m_s = reinterpret_cast< bitset_serialized* >(m_buf->bytes);
        }
        m_alignment_size = others.m_alignment_size;
        m_words_cap = others.m_words_cap;
        ::memcpy(static_cast<void*>(m_buf->bytes), static_cast<const void*>(others.m_buf->bytes), others.m_buf->size);
    }

    //
    // @brief Serialize the bitset and return the underlying serialized buffer that can be written as is (which can be
    // used to load later)
    //
    // NOTE: The returned buffer is a const byte array and thus it is expected not to be modified. If modified then it
    // can result in corruption to the bitset.
    //
    // @return sisl::byte_array
    //
    const sisl::byte_array serialize() const {
        if (ThreadSafeResizing) { m_lock.lock(); }
        const auto ret{m_buf};
        if (ThreadSafeResizing) { m_lock.unlock(); }
        return ret;
    }

    //
    // @brief Return the bytes it will have upon serializing
    //
    // @return uint64_t
    //
    uint64_t serialized_size() const {
        if (ThreadSafeResizing) { m_lock.lock(); }
        const auto sz{bitset_serialized::nbytes(m_s->m_nbits)};
        if (ThreadSafeResizing) { m_lock.unlock(); }
        return sz;
    }

    //
    // @brief Get total bits available in this bitset
    //
    // @return uint64_t
    //
    uint64_t size() const {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }
        const auto ret{total_bits()};
        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return ret;
    }

    uint64_t get_set_count() const {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }

        // get first word count which may be partial
        uint64_t set_cnt{0};
        const Word* word_ptr{get_word_const(0)};
        const uint8_t offset{get_word_offset(0)};
        set_cnt += get_set_bit_count(word_ptr->to_integer() >> offset);

        // count rest of words
        const uint64_t skip_bits{static_cast<uint64_t>(Word::bits() - offset)};
        uint64_t bits_remaining{skip_bits > total_bits() ? 0 : total_bits() - skip_bits};
        while (bits_remaining >= Word::bits())
        {
            set_cnt += (++word_ptr)->get_set_count();
            bits_remaining -= Word::bits();
        }

        // count last possibly partial word
        if (bits_remaining)
        {
            const uint8_t shift{static_cast<uint8_t>(Word::bits() - bits_remaining)};
            const uint64_t mask{(~static_cast< uint64_t >(0) << shift) >> shift};
            set_cnt += get_set_bit_count((++word_ptr)->to_integer() & mask);
        }

        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return set_cnt;
    }

    //
    // @brief Set the bit. If the bit is outside the available range throws std::out_of_range exception
    //
    // @param b Bit to set
    ///
    void set_bit(const uint64_t start) { set_reset_bit(start, true); }

    //
    // @brief Set multiple bits. If the bit is outside the available range throws std::out_of_range exception
    //
    // @param start Starting bit of the sequence to set
    // @param nbits Total number of bits from starting bit
    //
    void set_bits(const uint64_t start, const uint64_t nbits) { set_reset_bits(start, nbits, true); }

    //
    // @brief Reset the bit. If the bit is outside the available range throws std::out_of_range exception
    //
    // @param start Bit to reset
    //
    void reset_bit(const uint64_t start) { set_reset_bit(start, false); }

    //
    // @brief Reset multiple bits. If the bit is outside the available range throws std::out_of_range exception
    //
    // @param start Starting bit of the sequence to reset
    // @param nbits Total number of bits from starting bit
    //
    void reset_bits(const uint64_t start, const uint64_t nbits) { set_reset_bits(start, nbits, false); }

    //
    // @brief Is a particular bit is set/reset. If the bit is outside the available range throws std::out_of_range
    // exception
    //
    // @param start Starting bit of the sequence to check
    // @param nbits Total number of bits from starting bit
    //
    bool is_bits_set(const uint64_t start, const uint64_t nbits) const { return is_bits_set_reset(start, nbits, true); }
    bool is_bits_reset(const uint64_t start, const uint64_t nbits) const {
        return is_bits_set_reset(start, nbits, false);
    }

    //
    // @brief Get the value of the bit
    //
    // @param b Bit to get the value of
    // @return true or false based on if bit is set or reset respectively
    //
    bool get_bitval(const uint64_t bit) const {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }
        assert(m_s->valid_bit(bit));

        const Word* word_ptr{get_word_const(bit)};
        const uint8_t offset{get_word_offset(bit)};
        const bool ret{word_ptr->get_bitval(offset)};

        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return ret;
    }

    //
    // @brief Get the next set bit from given bit
    //
    // @param start_bit Start bit after which (inclusive) search for next bit is on
    // @return uint64_t Returns the next set bit, if one available, else Bitset::npos is returned
    //
    uint64_t get_next_set_bit(const uint64_t start_bit) {
        uint64_t ret{npos};
        if (ThreadSafeResizing) { m_lock.lock_shared(); }

        // check first word which may be partial
        const uint8_t offset{get_word_offset(start_bit)};
        const Word* word_ptr{get_word_const(start_bit)};
        if (word_ptr == nullptr)
        {
            if (ThreadSafeResizing) { m_lock.unlock_shared(); }
            return ret;
        }
        uint8_t nbit;
        if (word_ptr->get_next_set_bit(offset, &nbit)) {
            ret = start_bit + nbit - offset;
        }

        if (ret == npos) {
            // test rest of whole words
            uint64_t current_bit{start_bit + (Word::bits() - offset)};
            uint64_t bits_remaining{current_bit > total_bits() ? 0 : total_bits() - current_bit};
            while (bits_remaining >= Word::bits()) {
                if ((++word_ptr)->get_next_set_bit(0, &nbit)) {
                    ret = current_bit + nbit;
                    break;
                }
                current_bit += Word::bits();
                bits_remaining -= Word::bits();
            }

            // test last possible partial word
            if ((ret == npos) && (bits_remaining > 0)) {
                const uint8_t shift{static_cast< uint8_t >(Word::bits() - bits_remaining)};
                const uint64_t mask{(~static_cast< uint64_t >(0) << shift) >> shift};
                const Bitword< unsafe_bits< typename Word::word_t > > val{
                    static_cast< typename Word::word_t >((++word_ptr)->to_integer() & mask)};
                if (val.get_next_set_bit(0, &nbit)) { ret = current_bit + nbit; }
            }
        }

        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        if (ret >= total_bits()) ret = npos;
        return ret;
    }

    //
    // @brief Right shift the bitset with number of bits provided.
    // NOTE: To be efficient, This method does not immediately right shifts the entire set, rather set the marker and
    // once critical mass (typically 8K right shifts), it actually performs the move of data to right shift.
    //
    // @param nbits Total number of bits to right shift. If it is beyond total number of bits in the bitset, it throws
    // std::out_or_range exception.
    //
    void shrink_head(const uint64_t nbits) {
        if (ThreadSafeResizing) { m_lock.lock(); }

        if (nbits > total_bits()) { throw std::out_of_range("Right shift to out of range"); }
        m_s->m_skip_bits += nbits;
        if (m_s->m_skip_bits >= compaction_threshold()) { _resize(total_bits(), false); }

        if (ThreadSafeResizing) { m_lock.unlock(); }
    }

    //
    // @brief resize the bitset to number of bits. If nbits is more than existing bits, it will expand the bits and set
    // the new bits with value specified in the second parameter. If nbits is less than existing bits, it discards
    // remaining bits.
    //
    // @param nbits: New count of bits the bitset to be reset to
    // @param value: Value to set if bitset is resized up.
    //
    void resize(const uint64_t nbits, const bool value = false) {
        if (ThreadSafeResizing) { m_lock.lock(); }
        _resize(nbits, value);
        if (ThreadSafeResizing) { m_lock.unlock(); }
    }

    //
    // @brief Get the next contiguous n reset bits from the start bit
    //
    // @param start_bit Start bit to search from
    // @param n Count of required continous reset bits
    // @return BitBlock Retruns a BitBlock which provides the start bit and total number of bits found. Caller need to
    // check if returned count satisfies what is asked for.
    //
    BitBlock get_next_contiguous_n_reset_bits(const uint64_t start_bit, const uint32_t n) {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }

        BitBlock retb{start_bit, 0};
        uint32_t n_remaining{n};
        uint8_t offset{get_word_offset(start_bit)};
        uint64_t current_bit{start_bit};
        while (n_remaining > 0) {
            const Word* word_ptr{get_word_const(current_bit)};
            if (word_ptr == nullptr) { break; }

            const bit_filter filter{std::min<uint32_t>(n_remaining, Word::bits()), n, 1};
            const auto result{word_ptr->get_next_reset_bits_filtered(offset, filter)};

            if (result.match_type == bit_match_type::no_match) {
                // No match, reset everything to what it was before search.
                n_remaining = n;
                retb.start_bit = current_bit + Word::bits() - offset;
            } else if (result.match_type == bit_match_type::mid_match) {
                retb.start_bit = current_bit + result.start_bit - offset;
                retb.nbits = n;
                goto done;
            } else if (result.match_type == bit_match_type::msb_match) {
                // We didn't get what we want, but there are some residue bits, start creating a chain
                n_remaining = n - result.count;
                retb.start_bit = current_bit + result.start_bit - offset;
            } else if (result.match_type == bit_match_type::lsb_match) {
                // We got how much ever we need for leading bits. If we have enough to satisfy n
                // (note: We might need more than a word width), then respond.
                assert(offset == 0);
                if (n_remaining <= result.count) {
                    retb.nbits = n;
                    goto done;
                } else
                n_remaining -= result.count;
            }

            current_bit += (Word::bits() - offset);
            offset = 0;
        }

    done:
        if ((retb.start_bit + retb.nbits) > total_bits()) { retb = {npos, 0}; }
        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return retb;
    }

    //
    // @brief Get the next contiguous reset bits from the start bit upto n bits
    //
    // @param start_bit Start bit to search from
    // @param n Count of required continous reset bits
    // @return BitBlock Retruns a BitBlock which provides the start bit and total number of bits found. Caller need to
    // check if returned count satisfies what is asked for.
    //
    BitBlock get_next_contiguous_upto_n_reset_bits(const uint64_t start_bit, const uint32_t upto_n) {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }
        assert(m_s->valid_bit(start_bit));

        int offset = get_word_offset(start_bit);
        BitBlock retb = {0, 0};

        while (1) {
            Word* word = get_word(start_bit);
            if (word == nullptr) { break; }

            // Look for any free bits in the next iteration
            uint32_t nbits;
            retb.start_bit = start_bit + word->get_next_reset_bits(offset, &nbits);
            retb.nbits = nbits;
            if (nbits != 0) { break; }

            start_bit += (Word::bits() - offset);
            offset = 0;
        }

        while (retb.nbits < upto_n) {
            if (get_word_offset(retb.start_bit + retb.nbits) != 0) { break; }
            Word* word = get_word(retb.start_bit + retb.nbits);
            if (word == nullptr) { break; }
            uint32_t nbits;
            auto start_bit = word->get_next_reset_bits(0, &nbits);
            if (nbits == 0 || ((uint64_t)start_bit != (uint64_t)(retb.start_bit + retb.nbits))) { break; }
            retb.nbits += nbits;
            if (nbits < Word::bits()) { break; }
        }

        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return retb;
    }

    uint64_t get_next_reset_bit(const uint64_t start_bit) {
        uint64_t ret{npos};
        if (ThreadSafeResizing) { m_lock.lock_shared(); }

        // check first word which may be partial
        const uint8_t offset{get_word_offset(start_bit)};
        const Word* word_ptr{get_word_const(start_bit)};
        if (word_ptr == nullptr) {
            if (ThreadSafeResizing) { m_lock.unlock_shared(); }
            return ret;
        }
        uint8_t nbit;
        if (word_ptr->get_next_reset_bit(offset, &nbit)) {
            ret = start_bit + nbit - offset;
        }

        if (ret == npos) {
            // test rest of whole words
            uint64_t current_bit{start_bit + (Word::bits() - offset)};
            uint64_t bits_remaining{current_bit > total_bits() ? 0 : total_bits() - current_bit};
            while (bits_remaining >= Word::bits()) {
                if ((++word_ptr)->get_next_reset_bit(0, &nbit)) {
                    ret = current_bit + nbit;
                    break;
                }
                current_bit += Word::bits();
                bits_remaining -= Word::bits();
            }

            // test last possible partial word
            if ((ret == npos) && (bits_remaining > 0)) {
                const uint8_t shift{static_cast< uint8_t >(Word::bits() - bits_remaining)};
                const uint64_t mask{(~static_cast< uint64_t >(0) << shift) >> shift};
                const Bitword< unsafe_bits< typename Word::word_t > > val{
                    static_cast< typename Word::word_t >((++word_ptr)->to_integer() & mask)};
                if (val.get_next_reset_bit(0, &nbit)) { ret = current_bit + nbit; }
            }
        }

        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        if (ret >= total_bits()) ret = npos;
        return ret;
    }

    // This prints the entire word memory irregardless of skip bits and partial last word
    void print() {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }
        for (uint64_t i{0}; i < m_words_cap; ++i) {
            const Word* word_ptr{nth_word(i)};
            word_ptr->print();
        }
        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
    }

    // This prints the entire word memory irregardless of skip bits and partial last word
    std::string to_string() const {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }
        std::string out{};
        for (uint64_t i{0}; i < m_words_cap; ++i) {
            const Word* word_ptr{nth_word(i)};
            out += word_ptr->to_string();
        }
        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return out;
    }

private:
    void set_reset_bits(const uint64_t start, const uint64_t nbits, const bool value) {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }
        assert(m_s->valid_bit(start));

        // set first possibly partial word
        Word* word_ptr{get_word(start)};
        if (word_ptr == nullptr) {
            if (ThreadSafeResizing) { m_lock.unlock_shared(); }
            throw std::out_of_range("Set/Reset bits not in range");
        }
        const uint8_t offset{get_word_offset(start)};
        uint8_t count{static_cast< uint8_t >(
            (nbits > static_cast< uint8_t >(Word::bits() - offset)) ? (Word::bits() - offset) : nbits)};
        word_ptr->set_reset_bits(offset, count, value);

        // set rest of words
        uint64_t current_bit{start + count};
        uint64_t bits_remaining{nbits - count};
        const Word* end_words_ptr{m_s->end_words()};
        while ((bits_remaining > 0) && (++word_ptr != end_words_ptr)) {
            count = static_cast<uint8_t>((bits_remaining > Word::bits()) ? Word::bits() : bits_remaining);
            word_ptr->set_reset_bits(0, count, value);

            current_bit += count;
            bits_remaining -= count;
        }

        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        if (bits_remaining > 0) { throw std::out_of_range("Set/Reset bits not in range"); }
    }

    void set_reset_bit(const uint64_t bit, const bool value) {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }
        assert(m_s->valid_bit(bit));

        Word* word_ptr{get_word(bit)};
        const uint8_t offset{get_word_offset(bit)};
        word_ptr->set_reset_bits(offset, 1, value);

        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
    }

    bool is_bits_set_reset(const uint64_t start, const uint64_t nbits, const bool expected) const {
        if (ThreadSafeResizing) { m_lock.lock_shared(); }
        assert(m_s->valid_bit(start));

        // test first possibly partial word
        const Word* word_ptr{get_word_const(start)};
        const uint8_t offset{get_word_offset(start)};
        uint8_t count{static_cast< uint8_t >(
            (nbits > static_cast< uint8_t >(Word::bits() - offset)) ? (Word::bits() - offset) : nbits)};
        if (!word_ptr->is_bits_set_reset(offset, count, expected))
        {
            if (ThreadSafeResizing) { m_lock.unlock_shared(); }
            return false;
        }

        // test rest of words
        uint64_t current_bit{start + count};
        uint64_t bits_remaining{nbits - count};
        const Word* end_words_ptr{m_s->end_words()};
        while ((bits_remaining > 0) && (++word_ptr != end_words_ptr)) {
            count = static_cast< uint8_t >((bits_remaining > Word::bits()) ? Word::bits() : bits_remaining);
            if (!word_ptr->is_bits_set_reset(offset, count, expected))
            {
                if (ThreadSafeResizing) { m_lock.unlock_shared(); }
                return false;
            }

            current_bit += count;
            bits_remaining -= count;
        }

        if (ThreadSafeResizing) { m_lock.unlock_shared(); }
        return true;
    }

    void _resize(const uint64_t nbits, const bool value) {
        // We use the resize opportunity to compact bits. So we only to need to allocate nbits + first word skip
        // list size. Rest of them will be compacted.
        const uint64_t shrink_words{m_s->m_skip_bits / Word::bits()};
        const uint64_t new_skip_bits{m_s->m_skip_bits & m_word_mask};

        const uint64_t new_nbits{nbits + new_skip_bits};
        const uint64_t new_cap{bitset_serialized::total_words(new_nbits)};
        auto new_buf{make_byte_array(bitset_serialized::nbytes(new_nbits), m_alignment_size)};
        auto new_s{reinterpret_cast< bitset_serialized* >(new_buf->bytes)};

        const uint64_t move_nwords{std::min(m_words_cap - shrink_words, new_cap)};
        ::memmove(static_cast<void*>(new_s->m_words), static_cast<const void*>(&(m_s->m_words[shrink_words])),
                  sizeof(Word) * move_nwords);
        if (new_cap > move_nwords) {
            // Fill in the remaining space with value passed
            ::memset(static_cast<void*>(&(new_s->m_words[move_nwords])), value ? 0xff : 0x00,
                        sizeof(Word) * (new_cap - move_nwords));
        }

        m_words_cap = new_cap;
        m_buf = new_buf;
        m_s = new_s;
        m_s->m_skip_bits = new_skip_bits;
        m_s->m_nbits = new_nbits;

        LOGDEBUG("Resize to total_bits={} total_actual_bits={}, skip_bits={}, words_cap={}", total_bits(), m_s->m_nbits,
                 m_s->m_skip_bits, m_words_cap);
    }

    Word* get_word(const uint64_t bit) {
        const uint64_t offset{bit + m_s->m_skip_bits};
        return (sisl_unlikely(offset >= m_s->m_nbits)) ? nullptr : nth_word(offset / Word::bits());
    }

    const Word* get_word_const(const uint64_t bit) const {
        const uint64_t offset{bit + m_s->m_skip_bits};
        return (sisl_unlikely(offset >= m_s->m_nbits)) ? nullptr : nth_word(offset / Word::bits());
    }

    uint8_t get_word_offset(const uint64_t bit) const {
        const uint64_t offset{bit + m_s->m_skip_bits};
        return static_cast<uint8_t>(offset & m_word_mask);
    }

    uint64_t total_bits() const { return m_s->m_nbits - m_s->m_skip_bits; }

    Word* nth_word(const uint64_t word_n) { return &(m_s->m_words[word_n]); }
    const Word* nth_word(const uint64_t word_n) const { return &(m_s->m_words[word_n]); }
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
