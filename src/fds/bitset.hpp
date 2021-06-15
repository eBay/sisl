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
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wattributes"
#endif
#include <folly/SharedMutex.h>
#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic pop
#endif

#include <sds_logging/logging.h>

#include "bitword.hpp"
#include "buffer.hpp"

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
    ~BitBlock() = default;
};

template < typename Word, const bool ThreadSafeResizing = false >
class BitsetImpl {
public:
    typedef std::decay_t< Word > bitword_type;
    static_assert(bitword_type::bits() == 8 || bitword_type::bits() == 16 || bitword_type::bits() == 32 ||
                      bitword_type::bits() == 64,
                  "bitword_type::bits() must be power of two in size");
    typedef typename bitword_type::word_t word_t;
    typedef typename bitword_type::value_type value_type;

private:
#pragma pack(1)
    // NOTE: In the future may use a std::vector<Word> m_Words member and consolidate m_buf/m_s code
    struct bitset_serialized {
        // NOTE: The Words array follows directly in memory right after this structure
        uint64_t m_id; // persist ID for each bitmap. It is driven by user
        uint64_t m_nbits;
        uint64_t m_skip_bits;
        uint32_t m_alignment_size;
        uint64_t m_words_cap;
        uint32_t m_word_bits{bitword_type::bits()};

        bitset_serialized(const uint64_t id, const uint64_t nbits, const uint64_t skip_bits = 0,
                          const uint32_t alignment_size = 0, const bool fill = true) :
                m_id{id}, m_nbits{nbits}, m_skip_bits{skip_bits}, m_alignment_size{alignment_size},
                m_words_cap{total_words(nbits)} {
            // memory is unitialized so use uninitialized fill which does proper construction for non POD types
            if (fill) std::uninitialized_fill(get_words(), end_words(), bitword_type{});
        }
        bitset_serialized(const bitset_serialized&) = delete;
        bitset_serialized(bitset_serialized&&) noexcept = delete;
        bitset_serialized& operator=(const bitset_serialized&) = delete;
        bitset_serialized& operator=(bitset_serialized&&) noexcept = delete;
        ~bitset_serialized() {
            // destruct the Words
            std::destroy(get_words(), end_words());
        }

        bool valid_bit(const uint64_t bit) const { return (bit + m_skip_bits) < m_nbits; }
        bitword_type* end_words() { return std::next(get_words(), total_words(m_nbits)); }
        const bitword_type* end_words_const() const { return std::next(get_words_const(), total_words(m_nbits)); }
        bitword_type* get_words() {
            return reinterpret_cast< bitword_type* >(reinterpret_cast< uint8_t* >(this) + sizeof(bitset_serialized));
        }
        const bitword_type* get_words_const() const {
            return reinterpret_cast< const bitword_type* >(reinterpret_cast< const uint8_t* >(this) +
                                                   sizeof(bitset_serialized));
        }
        static constexpr uint64_t nbytes(const uint64_t nbits) {
            return sizeof(bitset_serialized) + total_words(nbits) * sizeof(bitword_type);
        }
        static constexpr uint64_t total_words(const uint64_t nbits) {
            return ((nbits / word_size()) + (((nbits & m_word_mask) > 0) ? 1 : 0));
        }
    };
#pragma pack()

    class ReadLockGuard {
    public:
        ReadLockGuard(const BitsetImpl* const bitset) : m_b{bitset} { lock(); }
        ReadLockGuard(const ReadLockGuard&) = delete;
        ReadLockGuard& operator=(const ReadLockGuard&) = delete;
        ReadLockGuard(ReadLockGuard&&) noexcept = delete;
        ReadLockGuard& operator=(ReadLockGuard&&) noexcept = delete;
        ~ReadLockGuard() { unlock(); }

        void lock() {
            if (ThreadSafeResizing) m_b->m_lock.lock_shared();
        };
        void unlock() {
            if (ThreadSafeResizing) m_b->m_lock.unlock_shared();
        }
        bool try_lock() { return (ThreadSafeResizing ? m_b->m_lock.try_lock_shared() : true); }

    private:
        const BitsetImpl* const m_b;
    };

    class WriteLockGuard {
    public:
        WriteLockGuard(BitsetImpl* const bitset) : m_b{bitset} { lock(); }
        WriteLockGuard(const WriteLockGuard&) = delete;
        WriteLockGuard& operator=(const WriteLockGuard&) = delete;
        WriteLockGuard(WriteLockGuard&&) noexcept = delete;
        WriteLockGuard& operator=(WriteLockGuard&&) noexcept = delete;
        ~WriteLockGuard() { unlock(); }

        void lock() {
            if (ThreadSafeResizing) m_b->m_lock.lock();
        };
        void unlock() {
            if (ThreadSafeResizing) m_b->m_lock.unlock();
        }
        bool try_lock() { return (ThreadSafeResizing ? m_b->m_lock.try_lock() : true); }

    private:
        BitsetImpl* const m_b;
    };

    sisl::byte_array m_buf;
    bitset_serialized* m_s{nullptr};
    mutable folly::SharedMutex m_lock;
    static constexpr uint64_t m_word_mask{bitword_type::bits() - 1};

#ifndef NDEBUG
    static constexpr size_t compaction_threshold() { return word_size() * 10; }
#else
    // Compact every 1K entries right shifted
    static constexpr size_t compaction_threshold() { return word_size() * 1024; }
#endif

public:
    static constexpr uint8_t word_size() { return bitword_type::bits(); }

    static constexpr uint64_t npos{std::numeric_limits< uint64_t >::max()};

    ~BitsetImpl() {
        {
            WriteLockGuard lock{this};
            if (m_buf && (m_buf.use_count() == 1)) m_s->~bitset_serialized();
            m_buf.reset();
        }
    }

    explicit BitsetImpl(const uint64_t nbits = 0, const uint64_t m_id = 0, const uint32_t alignment_size = 0) {
        const uint64_t size{(alignment_size > 0) ? round_up(bitset_serialized::nbytes(nbits), alignment_size)
                                                 : bitset_serialized::nbytes(nbits)};
        m_buf = make_byte_array(static_cast< uint32_t >(size), alignment_size, sisl::buftag::bitset);
        m_s = new (m_buf->bytes) bitset_serialized{m_id, nbits, 0, alignment_size};
    }

    // this makes a shared copy of the rhs so that modifications of the shared version
    // are also made to rhs version.  To make independent copy use copy function
    explicit BitsetImpl(const BitsetImpl& other) {
        ReadLockGuard lock{&other};
        m_buf = other.m_buf;
        m_s = other.m_s;
    }

    explicit BitsetImpl(const sisl::byte_array& b,
                        const std::optional< uint32_t > opt_alignment_size = std::optional< uint32_t >{}) {
        // NOTE: This assumes that the passed byte_array already has an initialized bitset_serialized structure
        // Also assume that the words byte array contains packed word_t data since packed Word data is illegal
        // with any class besides POD
        assert(b->size >= sizeof(bitset_serialized));
        // get the header info
        const bitset_serialized* const ptr{reinterpret_cast< const bitset_serialized* >(b->bytes)};
        assert(ptr->m_word_bits == bitword_type::bits());
        const uint64_t nbits{ptr->m_nbits};
        const uint64_t total_bytes{bitset_serialized::nbytes(nbits)};
        const uint32_t alignment_size{opt_alignment_size ? (*opt_alignment_size) : ptr->m_alignment_size};
        const uint64_t size{(alignment_size > 0) ? round_up(total_bytes, alignment_size) : total_bytes};
        assert(b->size >= total_bytes);
        m_buf = make_byte_array(static_cast< uint32_t >(size), alignment_size, sisl::buftag::bitset);
        m_s = new (m_buf->bytes) bitset_serialized{ptr->m_id, nbits, ptr->m_skip_bits, alignment_size, false};
        const word_t* b_words{reinterpret_cast< const word_t* >(b->bytes + sizeof(bitset_serialized))};
        // copy the data
        std::uninitialized_copy(b_words, std::next(b_words, m_s->m_words_cap), m_s->get_words());
    }

    explicit BitsetImpl(const word_t* const start_ptr, const word_t* const end_ptr, const uint64_t id = 0,
                        const uint32_t alignment_size = 0) {
        assert(end_ptr >= start_ptr);
        const size_t num_words{static_cast< size_t >(end_ptr - start_ptr)};
        const uint64_t nbits{static_cast< uint64_t >(num_words) * word_size()};
        const uint64_t size{(alignment_size > 0) ? round_up(bitset_serialized::nbytes(nbits), alignment_size)
                                                 : bitset_serialized::nbytes(nbits)};
        m_buf = make_byte_array(static_cast< uint32_t >(size), alignment_size, sisl::buftag::bitset);
        m_s = new (m_buf->bytes) bitset_serialized{id, nbits, 0, alignment_size, false};

        // copy the data into the uninitialized bitset
        std::uninitialized_copy(start_ptr, end_ptr, m_s->get_words());
    }

    template < typename IteratorType,
               typename = std::enable_if_t< std::is_same_v<
                   std::decay_t< typename std::iterator_traits< IteratorType >::value_type >, word_t > > >
    explicit BitsetImpl(const IteratorType& start_itr, const IteratorType& end_itr, const uint64_t id = 0,
                        const uint32_t alignment_size = 0) {
        const size_t num_words{static_cast< size_t >(std::distance(start_itr, end_itr))};
        const uint64_t nbits{static_cast< uint64_t >(num_words) * word_size()};
        const uint64_t size{(alignment_size > 0) ? round_up(bitset_serialized::nbytes(nbits), alignment_size)
                                                 : bitset_serialized::nbytes(nbits)};
        m_buf = make_byte_array(static_cast< uint32_t >(size), alignment_size, sisl::buftag::bitset);
        m_s = new (m_buf->bytes) bitset_serialized{id, nbits, 0, alignment_size, false};

        // copy the data into the unitialized bitset
        std::uninitialized_copy(start_itr, end_itr, m_s->get_words());
    }

    BitsetImpl(BitsetImpl&& other) noexcept {
        WriteLockGuard lock{&other};
        m_buf = std::move(other.m_buf);
        m_s = std::move(other.m_s);
        other.m_s = nullptr;
    }

    // this makes a shared copy of the rhs so that modifications of the shared version
    // are also made to rhs version.  To make independent copy use copy function
    BitsetImpl& operator=(const BitsetImpl& rhs) {
        if (this == &rhs) { return *this; }
        {
            WriteLockGuard lock{this};
            {
                ReadLockGuard rhs_lock{&rhs};
                // duplicate buffer if different
                if (m_buf != rhs.m_buf) {
                    // destroy original bitset if last one
                    if (m_buf.use_count() == 1) m_s->~bitset_serialized();

                    m_buf = rhs.m_buf;
                    m_s = rhs.m_s;
                }
            }
        }
        return *this;
    }

    BitsetImpl& operator=(BitsetImpl&& rhs) noexcept {
        if (this == &rhs) { return *this; }
        {
            WriteLockGuard lock{this};
            {
                WriteLockGuard rhs_lock{&rhs};
                if (m_buf != rhs.m_buf) {
                    // destroy original bitset if last one
                    if (m_buf.use_count() == 1) m_s->~bitset_serialized();

                    m_buf = std::move(rhs.m_buf);
                    m_s = std::move(rhs.m_s);
                } else {
                    // already sharing same buffer so just clear
                    rhs.m_buf.reset();
                }
                rhs.m_s = nullptr;
            }
        }
        return *this;
    }

    // get word size value.  data is stored in LSB to MSB order into the bitset
    word_t get_word_value(const uint64_t start_bit) const {
        ReadLockGuard lock{this};
        const bitword_type* word_ptr{get_word_const(start_bit)};
        if (!word_ptr) { return word_t{}; }

        word_t val{word_ptr->to_integer()};
        const uint8_t offset{get_word_offset(start_bit)};
        uint64_t bits_remaining{total_bits() - start_bit};
        if (offset > 0) {
            // compose from multiple words
            const uint8_t word_bits_remaining{static_cast< uint8_t >(word_size() - offset)};
            const uint8_t valid_low_bits{
                static_cast< uint8_t >((bits_remaining > word_bits_remaining) ? word_bits_remaining : bits_remaining)};
            const word_t low_mask{static_cast< word_t >(consecutive_bitmask[valid_low_bits - 1])};
            val = static_cast< word_t >(val >> offset) & low_mask;
            bits_remaining -= valid_low_bits;

            // add from next word if word_t size word spans two words
            if (bits_remaining > 0) {
                const uint8_t valid_high_bits{
                    static_cast< uint8_t >((bits_remaining > offset) ? offset : bits_remaining)};
                const word_t high_mask{static_cast< word_t >(consecutive_bitmask[valid_high_bits - 1])};
                val |= static_cast< word_t >(((++word_ptr)->to_integer() & high_mask) << valid_low_bits);
            }
        } else {
            // single word optimization
            if (bits_remaining < word_size()) {
                const word_t mask{static_cast< word_t >(consecutive_bitmask[bits_remaining - 1])};
                val &= mask;
            }
        }

        return val;
    }

    bool operator==(const BitsetImpl& rhs) const {
        if (this == &rhs) return true;
        {
            ReadLockGuard lock{this};
            {
                ReadLockGuard rhs_lock{&rhs};
                if (total_bits() != rhs.total_bits()) { return false; }

                uint64_t bits_remaining{total_bits()};
                if (bits_remaining == 0) { return true; }

                const bitword_type* lhs_word_ptr{get_word_const(0)};
                const bitword_type* rhs_word_ptr{rhs.get_word_const(0)};
                const uint8_t lhs_offset{get_word_offset(0)};
                const uint8_t rhs_offset{rhs.get_word_offset(0)};
                if (lhs_offset == rhs_offset) {
                    // optimized compare from equal offsets
                    if (lhs_offset > 0) {
                        // partial first word
                        const uint8_t word_bits_remaining{static_cast< uint8_t >(word_size() - lhs_offset)};
                        const uint8_t valid_bits{static_cast< uint8_t >(
                            (bits_remaining > word_bits_remaining) ? word_bits_remaining : bits_remaining)};
                        const word_t mask{static_cast< word_t >(consecutive_bitmask[valid_bits - 1])};
                        const word_t lhs_val{static_cast< word_t >(lhs_word_ptr->to_integer() >> lhs_offset) & mask};
                        const word_t rhs_val{static_cast< word_t >(rhs_word_ptr->to_integer() >> rhs_offset) & mask};
                        if (lhs_val != rhs_val) { return false; }
                        ++lhs_word_ptr;
                        ++rhs_word_ptr;
                        bits_remaining -= valid_bits;
                    }

                    // compare whole words
                    if (bits_remaining >= word_size()) {
                        const size_t num_words{static_cast< size_t >(bits_remaining / word_size())};
                        if (!std::equal(lhs_word_ptr, lhs_word_ptr + num_words, rhs_word_ptr)) { return false; }
                        lhs_word_ptr += num_words;
                        rhs_word_ptr += num_words;
                        bits_remaining -= static_cast< uint64_t >(num_words) * word_size();
                    }

                    // possible partial last word
                    if (bits_remaining > 0) {
                        const word_t mask{static_cast< word_t >(consecutive_bitmask[bits_remaining - 1])};
                        const word_t lhs_val{lhs_word_ptr->to_integer() & mask};
                        const word_t rhs_val{rhs_word_ptr->to_integer() & mask};
                        if (lhs_val != rhs_val) { return false; }
                    }
                } else {
                    // differing offsets
                    const uint8_t lhs_valid_low_bits{static_cast< uint8_t >(word_size() - lhs_offset)};
                    const word_t lhs_low_mask{static_cast< word_t >(consecutive_bitmask[lhs_valid_low_bits - 1])};
                    const uint8_t rhs_valid_low_bits{static_cast< uint8_t >(word_size() - rhs_offset)};
                    const word_t rhs_low_mask{static_cast< word_t >(consecutive_bitmask[rhs_valid_low_bits - 1])};
                    const word_t lhs_high_mask{
                        static_cast< word_t >((lhs_offset == 0) ? 0 : consecutive_bitmask[lhs_offset - 1])};
                    const word_t rhs_high_mask{
                        static_cast< word_t >((rhs_offset == 0) ? 0 : consecutive_bitmask[rhs_offset - 1])};

                    // compare whole words
                    while (bits_remaining >= word_size()) {
                        word_t lhs_val{(lhs_word_ptr++)->to_integer()}, rhs_val{(rhs_word_ptr++)->to_integer()};
                        if (lhs_offset > 0) {
                            lhs_val = (static_cast< word_t >(lhs_val >> lhs_offset) & lhs_low_mask) |
                                static_cast< word_t >((lhs_word_ptr->to_integer() & lhs_high_mask)
                                                      << lhs_valid_low_bits);
                        }
                        if (rhs_offset > 0) {
                            rhs_val = (static_cast< word_t >(rhs_val >> rhs_offset) & rhs_low_mask) |
                                static_cast< word_t >((rhs_word_ptr->to_integer() & rhs_high_mask)
                                                      << rhs_valid_low_bits);
                        }

                        if (lhs_val != rhs_val) { return false; }
                        bits_remaining -= word_size();
                    }

                    if (bits_remaining > 0) {
                        // compare partial last word
                        word_t lhs_val{(lhs_word_ptr++)->to_integer()}, rhs_val{(rhs_word_ptr++)->to_integer()};
                        const word_t mask{static_cast< word_t >(consecutive_bitmask[bits_remaining - 1])};
                        if (lhs_offset > 0) {
                            if (bits_remaining <= lhs_valid_low_bits) {
                                lhs_val = static_cast< word_t >(lhs_val >> lhs_offset) & mask;
                            } else {
                                const word_t mask{static_cast< word_t >(
                                    consecutive_bitmask[bits_remaining - lhs_valid_low_bits - 1])};
                                lhs_val = (static_cast< word_t >(lhs_val >> lhs_offset) & lhs_low_mask) |
                                    static_cast< word_t >((lhs_word_ptr->to_integer() & mask) << lhs_valid_low_bits);
                            }
                        } else {
                            lhs_val &= mask;
                        }
                        if (rhs_offset > 0) {
                            if (bits_remaining <= rhs_valid_low_bits) {
                                rhs_val = static_cast< word_t >(rhs_val >> rhs_offset) & mask;
                            } else {
                                const word_t mask{static_cast< word_t >(
                                    consecutive_bitmask[bits_remaining - rhs_valid_low_bits - 1])};
                                rhs_val = (static_cast< word_t >(rhs_val >> rhs_offset) & rhs_low_mask) |
                                    static_cast< word_t >((rhs_word_ptr->to_integer() & mask) << rhs_valid_low_bits);
                            }
                        } else {
                            rhs_val &= mask;
                        }

                        if (lhs_val != rhs_val) { return false; }
                    }
                }
            }
        }
        return true;
    }

    bool operator!=(const BitsetImpl& rhs) const { return !(operator==(rhs)); }

    uint64_t get_id() const {
        ReadLockGuard lock{this};
        assert(m_s);
        return m_s->m_id;
    }

    void set_id(const uint64_t id) {
        ReadLockGuard lock{this};
        assert(m_s);
        m_s->m_id = id;
    }

    // create deep copy of other bitset
    void copy(const BitsetImpl& other) {
        if (this == &other) return;
        {
            WriteLockGuard lock{this};
            {
                ReadLockGuard other_lock{&other};
                // ensure distinct buffers
                if ((m_buf->size != other.m_buf->size) || (m_buf == other.m_buf)) {
                    // destroy original bitset if last one
                    if (m_buf.use_count() == 1) m_s->~bitset_serialized();

                    m_buf = make_byte_array(other.m_buf->size, other.m_s->m_alignment_size, sisl::buftag::bitset);
                    m_s = new (m_buf->bytes)
                        bitset_serialized{other.m_s->m_id, other.m_s->m_nbits, other.m_s->m_skip_bits,
                                          other.m_s->m_alignment_size, false};
                    std::uninitialized_copy(other.m_s->get_words_const(), other.m_s->end_words_const(),
                                            m_s->get_words());
                } else {
                    // Word array is initialized here so std::copy suffices for some or all
                    const auto old_words_cap{m_s->m_words_cap};
                    if (other.m_s->m_words_cap > old_words_cap) {
                        m_s = new (m_buf->bytes)
                            bitset_serialized{other.m_s->m_id, other.m_s->m_nbits, other.m_s->m_skip_bits,
                                              other.m_s->m_alignment_size, false};
                        // copy into previously initialized spaces
                        std::copy(other.m_s->get_words_const(), std::next(other.m_s->get_words_const(), old_words_cap),
                                  m_s->get_words());
                        // unitialize copy the rest
                        std::uninitialized_copy(std::next(other.m_s->get_words_const(), old_words_cap),
                                                other.m_s->end_words_const(),
                                                std::next(m_s->get_words(), old_words_cap));
                    } else {
                        if (old_words_cap > other.m_s->m_words_cap) {
                            // destroy extra bitwords
                            std::destroy(std::next(m_s->get_words(), other.m_s->m_words_cap),
                                         std::next(m_s->get_words(), old_words_cap));
                        }

                        m_s = new (m_buf->bytes)
                            bitset_serialized{other.m_s->m_id, other.m_s->m_nbits, other.m_s->m_skip_bits,
                                              other.m_s->m_alignment_size, false};
                        std::copy(other.m_s->get_words_const(), other.m_s->end_words_const(), m_s->get_words());
                    }
                }
            }
        }
    }

    void copy_unshifted(const BitsetImpl& other) {
        if (this == &other) return;
        {
            WriteLockGuard lock{this};
            {
                ReadLockGuard other_lock{&other};
                const uint32_t alignment_size{other.m_s->m_alignment_size};
                const uint64_t nbits{other.total_bits()};
                const uint64_t size{(alignment_size > 0) ? round_up(bitset_serialized::nbytes(nbits), alignment_size)
                                                         : bitset_serialized::nbytes(nbits)};
                // ensure distinct buffers
                bool uninitialized{false};
                const auto old_words_cap{m_s->m_words_cap};
                if ((m_buf->size != size) || (m_buf == other.m_buf)) {
                    // destroy original bitset if last one
                    if (m_buf.use_count() == 1) m_s->~bitset_serialized();

                    m_buf = make_byte_array(size, alignment_size, sisl::buftag::bitset);
                    uninitialized = true;
                }
                m_s = new (m_buf->bytes) bitset_serialized{other.m_s->m_id, nbits, 0, alignment_size, false};
                const auto new_words_cap{m_s->m_words_cap};
                bitword_type* word_ptr{m_s->get_words()};
                const uint8_t rhs_offset{other.get_word_offset(0)};
                const bitword_type* rhs_word_ptr{other.get_word_const(0)};
                if (rhs_offset == 0) {
                    // can do straight copy
                    if (!uninitialized) {
                        if (new_words_cap > old_words_cap) {
                            // copy into previously initialized spaces
                            std::copy(rhs_word_ptr, std::next(rhs_word_ptr, old_words_cap), word_ptr);
                            // copy rest into unitialized
                            std::uninitialized_copy(std::next(rhs_word_ptr, old_words_cap),
                                                    other.m_s->end_words_const(), std::next(word_ptr, old_words_cap));
                        }
                        else {
                            if (old_words_cap > new_words_cap) {
                                // destroy extra bitwords
                                std::destroy(std::next(word_ptr, new_words_cap), std::next(word_ptr, old_words_cap));
                            } 
                            std::copy(rhs_word_ptr, other.m_s->end_words_const(), word_ptr);
                        }
                    } else {
                        std::uninitialized_copy(rhs_word_ptr, other.m_s->end_words_const(), word_ptr);
                    }
                } else {
                    // do word by word copy
                    uint64_t word_num{1};
                    uint64_t bits_remaining{nbits};
                    const uint8_t rhs_low_bits{
                        static_cast< uint8_t >(static_cast< uint8_t >(word_size() - rhs_offset))};
                    const word_t rhs_low_mask{static_cast< word_t >(consecutive_bitmask[rhs_low_bits - 1])};
                    const word_t rhs_high_mask{static_cast< word_t >(consecutive_bitmask[rhs_offset - 1])};
                    // copy whole words
                    while (bits_remaining >= word_size()) {
                        const word_t val{
                            (static_cast< word_t >(rhs_word_ptr->to_integer() >> rhs_offset) & rhs_low_mask) |
                            static_cast< word_t >(((rhs_word_ptr + 1)->to_integer() & rhs_high_mask) << rhs_low_bits)};
                        if (!uninitialized) {
                            if (word_num <= old_words_cap) {
                                word_ptr->set(val);
                            } else {
                                new (word_ptr) bitword_type{val};
                            }
                        } else {
                            new (word_ptr) bitword_type{val};
                        }
                        ++word_ptr;
                        ++rhs_word_ptr;
                        bits_remaining -= word_size();
                        ++word_num;
                    }

                    // copy partial last word
                    if (bits_remaining > 0) {
                        word_t val{(rhs_word_ptr++)->to_integer()};
                        const word_t mask{static_cast< word_t >(consecutive_bitmask[bits_remaining - 1])};
                        if (bits_remaining <= rhs_low_bits) {
                            val = static_cast< word_t >(val >> rhs_offset) & mask;
                        } else {
                            const word_t mask{
                                static_cast< word_t >(consecutive_bitmask[bits_remaining - rhs_low_bits - 1])};
                            val = (static_cast< word_t >(val >> rhs_offset) & rhs_low_mask) |
                                (static_cast< word_t >(rhs_word_ptr->to_integer() & mask) << rhs_low_bits);
                        }
                        if (!uninitialized) {
                            if (word_num <= old_words_cap) {
                                word_ptr->set(val);
                            } else {
                                new (word_ptr) bitword_type{val};
                            }
                        } else {
                            new (word_ptr) bitword_type{val};
                        }
                    }
                }
            }
        }
    }

    /**
     * @brief Serialize the bitset and return the underlying serialized buffer that can be written as is (which can be
     * used to load later)
     *
     * NOTE: The returned buffer is a const byte array and thus it is expected not to be modified. If modified then it
     * can result in corruption to the bitset.  Also if force_copy is false a shared version of the underlying byte
     * array may be returned which if other threads are operating on the same bitset can cause corruption if proper
     * locking not used.
     *
     * @return sisl::byte_array
     */
    const sisl::byte_array serialize(const std::optional< uint32_t > opt_alignment_size = std::optional< uint32_t >{},
                                     const bool force_copy = true) const {
        ReadLockGuard lock{this};
        assert(m_s);
        const uint64_t num_bits{total_bits()};
        const uint64_t total_words{bitset_serialized::total_words(num_bits)};
        const uint64_t total_bytes{sizeof(bitset_serialized) + sizeof(word_t) * total_words};
        const uint32_t alignment_size{opt_alignment_size ? (*opt_alignment_size) : m_s->m_alignment_size};
        const uint64_t size{(alignment_size > 0) ? round_up(total_bytes, alignment_size) : total_bytes};

        if (std::is_standard_layout_v< bitword_type > && std::is_trivial_v< value_type > &&
            (sizeof(value_type) == sizeof(bitword_type)) && (alignment_size == m_s->m_alignment_size) && !force_copy) {
            // underlying BitWord class is standard layout and same alignment
            // so return the underlying byte_array
            return m_buf;
        } else {
            // underlying BitWord is not standard layout or different alignment or copy
            auto buf{make_byte_array(size, alignment_size, sisl::buftag::bitset)};
            word_t* word_ptr{reinterpret_cast< word_t* >(buf->bytes + sizeof(bitset_serialized))};
            if (std::is_standard_layout_v< bitword_type > && std::is_trivial_v< value_type > &&
                (sizeof(value_type) == sizeof(bitword_type))) {
                const size_t num_words{static_cast< size_t >(m_s->end_words_const() - get_word_const(0))};
                const uint64_t skip_bits{get_word_offset(0)};
                new (buf->bytes) bitset_serialized{m_s->m_id, num_bits + skip_bits, skip_bits, alignment_size, false};
                std::memcpy(static_cast< void* >(word_ptr), static_cast< const void* >(get_word_const(0)),
                            num_words * sizeof(word_t));
            } else {
                // non trivial, word by word copy the unshifted data words
                new (buf->bytes) bitset_serialized{m_s->m_id, num_bits, 0, alignment_size, false};
                uint64_t current_bit{0};
                for (uint64_t word_num{0}; word_num < total_words; ++word_num, ++word_ptr, current_bit += word_size()) {
                    new (word_ptr) word_t{get_word_value(current_bit)};
                }
            }
            return buf;
        }
    }

    /**
     * @brief Return the bytes it will have upon serializing
     *
     * @return uint64_t
     */
    uint64_t serialized_size() const {
        ReadLockGuard lock{this};
        const uint64_t num_bits{total_bits()};
        const uint64_t total_words{bitset_serialized::total_words(num_bits)};
        const uint64_t total_bytes{sizeof(bitset_serialized) + sizeof(word_t) * total_words};
        return total_bytes;
    }

    /**
     * @brief Get total bits available in this bitset
     *
     * @return uint64_t
     */
    uint64_t size() const {
        ReadLockGuard lock{this};
        const auto ret{total_bits()};
        return ret;
    }

    /**
     * @brief Get the number of bits set in the range [start_bit, end_bit] inclusive
     *
     * @param start_bit Start bit to search from
     * @param end_bit End bit to search to inclusive; if larger than total bits to end
     *
     * @return returns the number of bits set
     */
    uint64_t get_set_count(const uint64_t start_bit = 0,
                           const uint64_t end_bit = std::numeric_limits< uint64_t >::max()) const {
        ReadLockGuard lock{this};
        assert(end_bit >= start_bit);
        const uint64_t last_bit{std::min(total_bits() - 1, end_bit)};
        const uint64_t num_bits{last_bit - start_bit + 1};

        // get first word count which may be partial and we assume that at least 1 word worth of bits
        uint64_t set_cnt{0};
        const bitword_type* word_ptr{get_word_const(start_bit)};
        if (!word_ptr) { return set_cnt; }
        const uint8_t offset{get_word_offset(start_bit)};
        if ((offset + num_bits) <= word_size()) {
            // all bits in first word
            const word_t mask{consecutive_bitmask[num_bits - 1]};
            set_cnt += get_set_bit_count((word_ptr->to_integer() >> offset) & mask);
        } else {
            set_cnt += get_set_bit_count(word_ptr->to_integer() >> offset);

            // count rest of words
            const uint64_t word_skip_bits{static_cast< uint64_t >(word_size() - offset)};
            uint64_t bits_remaining{word_skip_bits >= num_bits ? 0 : num_bits - word_skip_bits};
            while (bits_remaining >= word_size()) {
                set_cnt += (++word_ptr)->get_set_count();
                bits_remaining -= word_size();
            }

            // count last possibly partial word
            if (bits_remaining > 0) {
                const word_t mask{consecutive_bitmask[bits_remaining - 1]};
                set_cnt += get_set_bit_count(((++word_ptr)->to_integer()) & mask);
            }
        }
        return set_cnt;
    }

    /**
     * @brief Set the bit. If the bit is outside the available range throws std::out_of_range exception
     *
     * @param b Bit to set
     */
    void set_bit(const uint64_t start) { set_reset_bit(start, true); }

    /**
     * @brief Set multiple bits. If the bit is outside the available range throws std::out_of_range exception
     *
     * @param start Starting bit of the sequence to set
     * @param nbits Total number of bits from starting bit
     */
    void set_bits(const uint64_t start, const uint64_t nbits) { set_reset_bits(start, nbits, true); }

    /**
     * @brief Reset the bit. If the bit is outside the available range throws std::out_of_range exception
     *
     * @param start Bit to reset
     */
    void reset_bit(const uint64_t start) { set_reset_bit(start, false); }

    /**
     * @brief Reset multiple bits. If the bit is outside the available range throws std::out_of_range exception
     *
     * @param start Starting bit of the sequence to reset
     * @param nbits Total number of bits from starting bit
     */
    void reset_bits(const uint64_t start, const uint64_t nbits) { set_reset_bits(start, nbits, false); }

    /**
     * @brief Is a particular bit is set/reset. If the bit is outside the available range throws std::out_of_range
     * exception
     *
     * @param start Starting bit of the sequence to check
     * @param nbits Total number of bits from starting bit
     */
    bool is_bits_set(const uint64_t start, const uint64_t nbits) const { return is_bits_set_reset(start, nbits, true); }
    bool is_bits_reset(const uint64_t start, const uint64_t nbits) const {
        return is_bits_set_reset(start, nbits, false);
    }

    /**
     * @brief Get the value of the bit
     *
     * @param b Bit to get the value of
     * @return true or false based on if bit is set or reset respectively
     */
    bool get_bitval(const uint64_t bit) const {
        ReadLockGuard lock{this};
        assert(m_s->valid_bit(bit));

        const bitword_type* word_ptr{get_word_const(bit)};
        if (!word_ptr) { return false; }
        const uint8_t offset{get_word_offset(bit)};
        const bool ret{word_ptr->get_bitval(offset)};

        return ret;
    }

    /**
     * @brief Get the next set bit from given bit
     *
     * @param start_bit Start bit after which (inclusive) search for next bit is on
     * @return uint64_t Returns the next set bit, if one available, else Bitset::npos is returned
     */
    uint64_t get_next_set_bit(const uint64_t start_bit) const {
        ReadLockGuard lock{this};
        uint64_t ret{npos};

        // check first word which may be partial
        const uint8_t offset{get_word_offset(start_bit)};
        const bitword_type* word_ptr{get_word_const(start_bit)};
        if (!word_ptr) { return ret; }
        uint8_t nbit{};
        if (word_ptr->get_next_set_bit(offset, &nbit)) { ret = start_bit + nbit - offset; }

        if (ret == npos) {
            // test rest of whole words
            uint64_t current_bit{start_bit + (word_size() - offset)};
            uint64_t bits_remaining{current_bit > total_bits() ? 0 : total_bits() - current_bit};
            while (bits_remaining > 0) {
                if ((++word_ptr)->get_next_set_bit(0, &nbit)) {
                    ret = current_bit + nbit;
                    break;
                }
                current_bit += word_size();
                bits_remaining -= std::min< uint64_t >(bits_remaining, word_size());
            }
        }

        if (ret >= total_bits()) ret = npos;
        return ret;
    }

    /**
     * @brief Right shift the bitset with number of bits provided.
     * NOTE: To be efficient, This method does not immediately right shifts the entire set, rather set the marker
     * and once critical mass (typically 8K right shifts), it actually performs the move of data to right shift.
     *
     * @param nbits Total number of bits to right shift. If it is beyond total number of bits in the bitset, it
     * throws std::out_or_range exception.
     */
    void shrink_head(const uint64_t nbits) {
        WriteLockGuard lock{this};
        assert(m_s);

        if (nbits > total_bits()) {
            throw std::out_of_range("Right shift to out of range");
        } else {
            m_s->m_skip_bits += nbits;
            if (m_s->m_skip_bits >= compaction_threshold()) { resize_impl(total_bits(), false); }
        }
    }

    /**
     * @brief resize the bitset to number of bits. If nbits is more than existing bits, it will expand the bits and
     * set the new bits with value specified in the second parameter. If nbits is less than existing bits, it
     * discards remaining bits.
     *
     * @param nbits: New count of bits the bitset to be reset to
     * @param value: Value to set if bitset is resized up.
     */
    void resize(const uint64_t nbits, const bool value = false) {
        WriteLockGuard lock{this};
        resize_impl(nbits, value);
    }

    /**
     * @brief Get the next contiguous n reset bits from the start bit
     *
     * @param start_bit Start bit to search from
     * @param n Count of required continuous reset bits
     * @return BitBlock Returns a BitBlock which provides the start bit and total number of bits found. Caller need
     * to check if returned count satisfies what is asked for.
     */
    BitBlock get_next_contiguous_n_reset_bits(const uint64_t start_bit, const uint32_t n) const {
        return get_next_contiguous_n_reset_bits(start_bit, std::nullopt, n, n);
    }

    // A backward compatible API
    BitBlock get_next_contiguous_upto_n_reset_bits(const uint64_t start_bit, const uint32_t n) const {
        return get_next_contiguous_n_reset_bits(start_bit, std::nullopt, n, n);
    }

    /**
     * @brief Get the next contiguous [min_needed, max_needed] inclusive reset bits in the range
     * [start_bit, end_bit] inclusive
     *
     * @param start_bit Start bit to search from
     * @param end_bit Optional End bit to search to inclusive; otherwise to end of set
     * @param min_needed Minimum number of reset bits needed
     * @param max_needed Maximum number of reset bits needed
     *
     * @return BitBlock Returns a BitBlock which provides the start bit and total number of bits found. Caller need
     * to check if returned count satisfies what is asked for.
     */
    BitBlock get_next_contiguous_n_reset_bits(const uint64_t start_bit, const std::optional< uint64_t > end_bit,
                                              const uint32_t min_needed, const uint32_t max_needed) const {
        ReadLockGuard lock{this};
        BitBlock retb{start_bit, 0};

        const bitword_type* word_ptr{get_word_const(start_bit)};
        if (!word_ptr) { return {npos, 0}; }
        uint8_t offset{get_word_offset(start_bit)};
        uint64_t current_bit{start_bit};
        const uint64_t final_bit{end_bit ? std::min(*end_bit + 1, total_bits()) : total_bits()};

        while ((retb.nbits < max_needed) && (current_bit < final_bit)) {
            const bit_filter filter{(retb.nbits >= min_needed)
                                        ? static_cast< uint32_t >(1)
                                        : std::min< uint32_t >(min_needed - retb.nbits, word_size()),
                                    min_needed, static_cast< uint32_t >(1)};
            const auto result{word_ptr->get_next_reset_bits_filtered(offset, filter)};
            LOGTRACE("current_bit={} word filter={} result={}", current_bit, filter.to_string(), result.to_string());

            if (result.match_type == bit_match_type::full_match) {
                // We got the entire word, keep adding to the chain
                assert(offset == 0);
                retb.nbits += result.count;
            } else if (result.match_type == bit_match_type::lsb_match) {
                // We got atleast min from the chain, keep adding to the chain
                assert(offset == 0);
                retb.nbits += result.count;
                if (retb.nbits >= min_needed) { break; }
            } else if (result.match_type == bit_match_type::mid_match) {
                assert(result.count >= min_needed);
                if (result.count > retb.nbits) { retb = {current_bit + result.start_bit - offset, result.count}; }
                break;
            } else if (result.match_type == bit_match_type::msb_match) {
                if (retb.nbits >= min_needed) { break; } // It has met the min with previous scan, use it - greedy algo
                retb = {current_bit + result.start_bit - offset, result.count};
            } else if (result.match_type == bit_match_type::no_match) {
                if (retb.nbits >= min_needed) { break; } // It has met the min with previous scan, use it - greedy algo
                retb = {current_bit + word_size() - offset, 0}; // Reset everything and start over
            }

            current_bit += (word_size() - offset);
            offset = 0;
            ++word_ptr;
        }

        if (retb.nbits > 0) {
            // Do alignment adjustments if need be
            if ((retb.start_bit + retb.nbits) > final_bit) {
                // It is an unlikely path - only when total bits are not 64 bit aligned and retb happens to be at
                // the end
                if (retb.start_bit >= final_bit) {
                    retb = {npos, 0};
                } else {
                    retb.nbits = static_cast< uint32_t >(final_bit - retb.start_bit);
                }
            }
            // Note: these belong here since must be done after above since nbits may be reduced
            if (retb.nbits > max_needed) { retb.nbits = max_needed; }
            if (retb.nbits < min_needed) { retb = {npos, 0}; }
        } else {
            retb.start_bit = npos;
        }

        return retb;
    }

    uint64_t get_next_reset_bit(const uint64_t start_bit) const {
        ReadLockGuard lock{this};
        uint64_t ret{npos};

        // check first word which may be partial
        const bitword_type* word_ptr{get_word_const(start_bit)};
        if (!word_ptr) { return ret; }
        const uint8_t offset{get_word_offset(start_bit)};
        uint8_t nbit{};
        if (word_ptr->get_next_reset_bit(offset, &nbit)) { ret = start_bit + nbit - offset; }

        if (ret == npos) {
            // test rest of whole words
            uint64_t current_bit{start_bit + (word_size() - offset)};
            uint64_t bits_remaining{current_bit > total_bits() ? 0 : total_bits() - current_bit};
            while (bits_remaining > 0) {
                if ((++word_ptr)->get_next_reset_bit(0, &nbit)) {
                    ret = current_bit + nbit;
                    break;
                }
                current_bit += word_size();
                bits_remaining -= std::min< uint64_t >(bits_remaining, word_size());
            }
        }

        if (ret >= total_bits()) ret = npos;
        return ret;
    }

    void print() const { std::cout << to_string() << std::endl; }

    // print out the bitset in the order last bit to first bit
    std::string to_string() const {
        ReadLockGuard lock{this};
        std::string output{};
        output.reserve(total_bits());

        if (total_bits() == 0) { return output; }

        uint64_t bits_remaining{total_bits()};
        const bitword_type* word_ptr{get_word_const(total_bits() - 1)};
        const uint8_t offset{get_word_offset(total_bits() - 1)};
        // get last word possibly partial word if does not end on high bit
        if (offset < (word_size() - 1)) {
            const word_t val{(word_ptr--)->to_integer()};
            const uint8_t valid_bits{static_cast< uint8_t >(
                (bits_remaining > static_cast< uint64_t >(offset + 1)) ? (offset + 1) : bits_remaining)};
            word_t mask{static_cast< word_t >(bit_mask[offset])};
            for (uint8_t bit{0}; bit < valid_bits; ++bit, mask >>= 1) {
                output.push_back((((val & mask) == mask) ? '1' : '0'));
            }
            bits_remaining -= valid_bits;
        }

        // print whole words
        while (bits_remaining >= word_size()) {
            const word_t val{(word_ptr--)->to_integer()};
            word_t mask{static_cast< word_t >(bit_mask[word_size() - 1])};
            for (uint8_t bit{0}; bit < word_size(); ++bit, mask >>= 1) {
                output.push_back((((val & mask) == mask) ? '1' : '0'));
            }
            bits_remaining -= word_size();
        }

        // get first word possibly partial word
        if (bits_remaining > 0) {
            const word_t val{word_ptr->to_integer()};
            word_t mask{static_cast< word_t >(bit_mask[word_size() - 1])};
            for (uint8_t bit{0}; bit < bits_remaining; ++bit, mask >>= 1) {
                output.push_back((((val & mask) == mask) ? '1' : '0'));
            }
        }

        return output;
    }

private:
    void set_reset_bits(const uint64_t start, const uint64_t nbits, const bool value) {
        ReadLockGuard lock{this};
        assert(m_s && m_s->valid_bit(start));

        // NOTE: we ignore the fact here that the total number of bits may not consume the entire
        // last word
        // set first possibly partial word
        bitword_type* word_ptr{get_word(start)};
        if (!word_ptr) { throw std::out_of_range("Set/Reset bits not in range"); }
        const uint8_t offset{get_word_offset(start)};
        uint8_t count{static_cast< uint8_t >(
            (nbits > static_cast< uint8_t >(word_size() - offset)) ? (word_size() - offset) : nbits)};
        word_ptr->set_reset_bits(offset, count, value);

        // set rest of words
        uint64_t current_bit{start + count};
        uint64_t bits_remaining{nbits - count};
        const bitword_type* const end_words_ptr{m_s->end_words_const()};
        while ((bits_remaining > 0) && (++word_ptr != end_words_ptr)) {
            count = static_cast< uint8_t >((bits_remaining > word_size()) ? word_size() : bits_remaining);
            word_ptr->set_reset_bits(0, count, value);

            current_bit += count;
            bits_remaining -= count;
        }

        if (bits_remaining > 0) { throw std::out_of_range("Set/Reset bits not in range"); }
    }

    void set_reset_bit(const uint64_t bit, const bool value) {
        ReadLockGuard lock{this};
        assert(m_s && m_s->valid_bit(bit));

        bitword_type* word_ptr{get_word(bit)};
        if (!word_ptr) { return; }
        const uint8_t offset{get_word_offset(bit)};
        word_ptr->set_reset_bits(offset, 1, value);
    }

    bool is_bits_set_reset(const uint64_t start, const uint64_t nbits, const bool expected) const {
        ReadLockGuard lock{this};
        assert(m_s && m_s->valid_bit(start));

        // test first possibly partial word
        const bitword_type* word_ptr{get_word_const(start)};
        if (!word_ptr) { return (nbits == 0); }
        uint64_t bits_remaining{(nbits > total_bits() - start) ? total_bits() - start : nbits};
        const uint8_t offset{get_word_offset(start)};
        uint8_t count{static_cast< uint8_t >(
            (bits_remaining > static_cast< uint8_t >(word_size() - offset)) ? (word_size() - offset) : bits_remaining)};
        if (!word_ptr->is_bits_set_reset(offset, count, expected)) { return false; }

        // test rest of words
        uint64_t current_bit{start + count};
        bits_remaining -= count;
        const bitword_type* const end_words_ptr{m_s->end_words_const()};
        while ((bits_remaining > 0) && (++word_ptr != end_words_ptr)) {
            count = static_cast< uint8_t >((bits_remaining > word_size()) ? word_size() : bits_remaining);
            if (!word_ptr->is_bits_set_reset(0, count, expected)) { return false; }

            current_bit += count;
            bits_remaining -= count;
        }

        return (bits_remaining == 0);
    }

private:
    // NOTE: This function should be called under a write lock
    void resize_impl(const uint64_t nbits, const bool value) {
        // We use the resize opportunity to compact bits. So we only to need to allocate nbits + first word skip
        // list size. Rest of them will be compacted.
        assert(m_s);
        const uint64_t shrink_words{m_s->m_skip_bits / word_size()};
        const uint64_t new_skip_bits{m_s->m_skip_bits & m_word_mask};

        const uint64_t new_nbits{nbits + new_skip_bits};
        auto new_buf{
            make_byte_array(bitset_serialized::nbytes(new_nbits), m_s->m_alignment_size, sisl::buftag::bitset)};
        auto new_s{new (new_buf->bytes)
                       bitset_serialized{m_s->m_id, new_nbits, new_skip_bits, m_s->m_alignment_size, false}};
        const auto new_cap{new_s->m_words_cap};

        // copy to resized
        const uint64_t move_nwords{std::min(m_s->m_words_cap - shrink_words, new_cap)};
        std::uninitialized_copy(std::next(m_s->get_words_const(), shrink_words), m_s->end_words_const(), new_s->get_words());
        if (new_cap > move_nwords) {
            // Fill in the remaining space with value passed
            std::uninitialized_fill(std::next(new_s->get_words(), move_nwords), new_s->end_words(),
                                    (value ? bitword_type{static_cast< word_t >(~word_t{})} : bitword_type{word_t{}}));
        }

        // destroy original if last 1
        if (m_buf.use_count() == 1) m_s->~bitset_serialized();

        // swap old with new
        m_buf = new_buf;
        m_s = new_s;

        LOGDEBUG("Resize to total_bits={} total_actual_bits={}, skip_bits={}, words_cap={}", total_bits(), m_s->m_nbits,
                 m_s->m_skip_bits, m_s->m_words_cap);
    }

    // NOTE: must be called under lock
    bitword_type* get_word(const uint64_t bit) {
        assert(m_s);
        const uint64_t offset{bit + m_s->m_skip_bits};
        return (sisl_unlikely(offset >= m_s->m_nbits)) ? nullptr : nth_word(offset / word_size());
    }

    // NOTE: must be called under lock
    const bitword_type* get_word_const(const uint64_t bit) const {
        assert(m_s);
        const uint64_t offset{bit + m_s->m_skip_bits};
        return (sisl_unlikely(offset >= m_s->m_nbits)) ? nullptr : nth_word(offset / word_size());
    }

    // NOTE: must be called under lock
    uint8_t get_word_offset(const uint64_t bit) const {
        assert(m_s);
        const uint64_t offset{bit + m_s->m_skip_bits};
        return static_cast< uint8_t >(offset & m_word_mask);
    }

    // NOTE: must be called under lock
    uint64_t total_bits() const {
        assert(m_s);
        return (m_s->m_nbits - m_s->m_skip_bits);
    }

    // NOTE: must be called under lock
    bitword_type* nth_word(const uint64_t word_n) {
        assert(m_s);
        return &(m_s->get_words()[word_n]);
    }
    // NOTE: must be called under lock
    const bitword_type* nth_word(const uint64_t word_n) const {
        assert(m_s);
        return &(m_s->get_words()[word_n]);
    }
};

template < typename charT, typename traits, typename Word, bool ThreadSafeResizing = false >
std::basic_ostream< charT, traits >& operator<<(std::basic_ostream< charT, traits >& out_stream,
                                                const BitsetImpl< Word, ThreadSafeResizing >& bitset) {
    // copy the stream formatting
    std::basic_ostringstream< charT, traits > out_stream_working;
    out_stream_working.copyfmt(out_stream);

    // output the date time
    out_stream_working << bitset.to_string();

    // print the stream
    out_stream << out_stream_working.str();

    return out_stream;
}

// external comparison functions
template < typename Word, const bool ThreadSafeResizing >
inline bool operator==(const BitsetImpl< Word, ThreadSafeResizing >& bitset1,
                       const BitsetImpl< Word, ThreadSafeResizing >& bitset2) {
    return bitset1.operator==(bitset2);
}

template < typename Word, const bool ThreadSafeResizing >
inline bool operator!=(const BitsetImpl< Word, ThreadSafeResizing >& bitset1,
                       const BitsetImpl< Word, ThreadSafeResizing >& bitset2) {
    return bitset1.operator!=(bitset2);
}

/**
 * @brief Bitset: Plain bitset with no safety. Concurrent updates and access are not thread safe and it is
 * expected the user to handle that. This is equivalent to boost::dynamic_bitset
 */
typedef BitsetImpl< Bitword< unsafe_bits< uint64_t > >, false > Bitset;

/**
 * @brief AtomicBitset: The only thread safety this version provides is concurrently 2 different bits can be
 * set/unset. However, set/unset concurrently along with increasing the size, setting a bit beyond original
 * size, concurrent test of bits can produce inconsistent values
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
