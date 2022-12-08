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
#pragma once

#include <cstdint>
#include <cassert>
#include <memory>
#include <mutex>

#include "bitset.hpp"
#include "utils.hpp"

namespace sisl {
class IDReserver {
public:
    IDReserver(uint32_t estimated_ids = 1024) : m_reserved_bits(estimated_ids) { assert(estimated_ids != 0); }

    IDReserver(const sisl::byte_array& b) : m_reserved_bits(b) {}

    uint32_t reserve() {
        std::unique_lock lg(m_mutex);
        size_t nbit = m_reserved_bits.get_next_reset_bit(0);
        if (nbit == Bitset::npos) {
            // We ran out of room to allocate bits, resize and allocate more
            const auto cur_size = m_reserved_bits.size();
            assert(cur_size != 0);
            m_reserved_bits.resize(cur_size * 2);
            nbit = cur_size;
        }
        m_reserved_bits.set_bit(nbit);
        return nbit;
    }

    void reserve(uint32_t id) {
        std::unique_lock lg(m_mutex);
        assert(!(m_reserved_bits.get_bitval(id)));
        assert(id < m_reserved_bits.size());
        m_reserved_bits.set_bit(id);
    }

    void unreserve(uint32_t id) {
        std::unique_lock lg(m_mutex);
        assert(id < m_reserved_bits.size());
        m_reserved_bits.reset_bit(id);
    }

    bool is_reserved(uint32_t id) {
        std::unique_lock lg(m_mutex);
        return m_reserved_bits.get_bitval(id);
    }

    sisl::byte_array serialize() {
        std::unique_lock lg(m_mutex);
        return m_reserved_bits.serialize();
    }

    bool first_reserved_id(uint32_t& found_id) { return find_next_reserved_id(true, found_id); }
    bool next_reserved_id(uint32_t& last_found_id) { return find_next_reserved_id(false, last_found_id); }

private:
    bool find_next_reserved_id(bool first, uint32_t& last_found_id) {
        std::unique_lock lg(m_mutex);
        size_t nbit = m_reserved_bits.get_next_set_bit(first ? 0 : last_found_id + 1);
        if (nbit == Bitset::npos) return false;
        last_found_id = (uint32_t)nbit;
        return true;
    }

private:
    std::mutex m_mutex;
    sisl::Bitset m_reserved_bits;
};
} // namespace sisl
