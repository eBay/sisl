/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam
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
#include <sisl/fds/bitword.hpp>
#include <sisl/fds/utils.hpp>
#include <sisl/fds/buffer.hpp>

namespace sisl {
class CompactBitSet {
public:
    using bit_count_t = uint32_t;

private:
    using bitword_type = Bitword< unsafe_bits< uint64_t > >;

    struct serialized {
        bitword_type words[1]{bitword_type{}};
    };

    bit_count_t nbits_{0};
    bool allocated_{false};
    serialized* s_{nullptr};

private:
    static constexpr size_t word_size_bytes() { return sizeof(unsafe_bits< uint64_t >); }
    static constexpr size_t word_size_bits() { return word_size_bytes() * 8; }
    static constexpr uint64_t word_mask() { return bitword_type::bits() - 1; }

public:
    static constexpr bit_count_t inval_bit = std::numeric_limits< bit_count_t >::max();
    static constexpr uint8_t size_multiples() { return word_size_bytes(); }

    explicit CompactBitSet(bit_count_t nbits) {
        DEBUG_ASSERT_GT(nbits, 0, "compact bitset should have nbits > 0");
        nbits_ = s_cast< bit_count_t >(sisl::round_up(nbits, word_size_bits()));
        size_t const buf_size = nbits_ / 8;

        uint8_t* buf = new uint8_t[buf_size];
        std::memset(buf, 0, buf_size);
        s_ = r_cast< serialized* >(buf);
        allocated_ = true;
    }

    CompactBitSet(sisl::blob buf, bool init_bits) : s_{r_cast< serialized* >(buf.bytes())} {
        DEBUG_ASSERT_GT(buf.size(), 0, "compact bitset initialized with empty buffer");
        DEBUG_ASSERT_EQ(buf.size() % word_size_bytes(), 0, "compact bitset buffer size must be multiple of word size");
        nbits_ = buf.size() * 8;
        if (init_bits) { std::memset(buf.bytes(), 0, buf.size()); }
    }

    ~CompactBitSet() {
        if (allocated_) { delete[] uintptr_cast(s_); }
    }

    bit_count_t size() const { return nbits_; }
    void set_bit(bit_count_t start) { set_reset_bit(start, true); }
    void reset_bit(bit_count_t start) { set_reset_bit(start, false); }

    bool is_bit_set(bit_count_t bit) const {
        bitword_type const* word_ptr = get_word_const(bit);
        if (!word_ptr) { return false; }
        uint8_t const offset = get_word_offset(bit);
        return word_ptr->is_bit_set_reset(offset, true);
    }

    bit_count_t get_next_set_bit(bit_count_t start_bit) const { return get_next_set_or_reset_bit(start_bit, true); }
    bit_count_t get_next_reset_bit(bit_count_t start_bit) const { return get_next_set_or_reset_bit(start_bit, false); }

    /// @brief This method gets the previous set bit from starting bit (including the start bit). So if start bit
    /// is 1, it will return the start bit.
    /// @param start_bit: Start bit should be > 0 and <= size()
    /// @return Returns the previous set bit or inval_bit if nothing is set
    bit_count_t get_prev_set_bit(bit_count_t start_bit) const {
        // check first word which may be partial
        uint8_t offset = get_word_offset(start_bit);
        bit_count_t word_idx = get_word_index(start_bit);

        do {
            bitword_type const* word_ptr = &s_->words[word_idx];
            if (!word_ptr) { return inval_bit; }

            uint8_t nbit{0};
            if (word_ptr->get_prev_set_bit(offset, &nbit)) { return start_bit - (offset - nbit); }

            start_bit -= offset;
            offset = bitword_type::bits();
        } while (word_idx-- != 0);

        return inval_bit;
    }

    void set_reset_bit(bit_count_t bit, bool value) {
        bitword_type* word_ptr = get_word(bit);
        if (!word_ptr) { return; }
        uint8_t const offset = get_word_offset(bit);
        word_ptr->set_reset_bits(offset, 1, value);
    }

    bit_count_t get_next_set_or_reset_bit(bit_count_t start_bit, bool search_for_set_bit) const {
        bit_count_t ret{inval_bit};

        // check first word which may be partial
        uint8_t const offset = get_word_offset(start_bit);
        bitword_type const* word_ptr = get_word_const(start_bit);
        if (!word_ptr) { return ret; }

        uint8_t nbit{0};
        bool found = search_for_set_bit ? word_ptr->get_next_set_bit(offset, &nbit)
                                        : word_ptr->get_next_reset_bit(offset, &nbit);
        if (found) { ret = start_bit + nbit - offset; }

        if (ret == inval_bit) {
            // test rest of whole words
            bit_count_t current_bit = start_bit + (bitword_type::bits() - offset);
            bit_count_t bits_remaining = (current_bit > size()) ? 0 : size() - current_bit;
            while (bits_remaining > 0) {
                ++word_ptr;
                found =
                    search_for_set_bit ? word_ptr->get_next_set_bit(0, &nbit) : word_ptr->get_next_reset_bit(0, &nbit);
                if (found) {
                    ret = current_bit + nbit;
                    break;
                }
                current_bit += bitword_type::bits();
                bits_remaining -= std::min< bit_count_t >(bits_remaining, bitword_type::bits());
            }
        }

        if (ret >= size()) { ret = inval_bit; }
        return ret;
    }

    std::string to_string() const {
        std::string str;
        auto const num_words = size() / word_size_bits();
        for (uint32_t i{0}; i < num_words; ++i) {
            fmt::format_to(std::back_inserter(str), "{}", s_->words[i].to_string());
        }
        return str;
    }

private:
    bitword_type* get_word(bit_count_t bit) {
        return (sisl_unlikely(bit >= nbits_)) ? nullptr : &s_->words[bit / word_size_bits()];
    }

    bitword_type const* get_word_const(bit_count_t bit) const {
        return (sisl_unlikely(bit >= nbits_)) ? nullptr : &s_->words[bit / word_size_bits()];
    }

    bit_count_t get_word_index(bit_count_t bit) const {
        DEBUG_ASSERT(s_, "compact bitset not initialized");
        return bit / word_size_bits();
    }

    uint8_t get_word_offset(bit_count_t bit) const {
        assert(s_);
        return static_cast< uint8_t >(bit & word_mask());
    }
};
} // namespace sisl
