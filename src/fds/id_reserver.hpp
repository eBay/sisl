/*
 * id_reserver.hpp
 *
 *  Created on: 19-Nov-2019
 *      Author: hkadayam
 */
#pragma once

#include <fds/bitset.hpp>
#include <mutex>

namespace sisl {
class IDReserver {
public:
    IDReserver(uint32_t estimated_ids = 1024) : m_reserved_bits(estimated_ids) {}
    IDReserver(const sisl::byte_array& b) : m_reserved_bits(b) {}

    uint32_t reserve() {
        std::unique_lock lg(m_mutex);
        size_t nbit = m_reserved_bits.get_next_reset_bit(0);
        if (nbit == Bitset::npos) {
            // We ran out of room to allocate bits, resize and allocate more
            auto cur_size = m_reserved_bits.size();
            m_reserved_bits.resize(cur_size * 2);
            nbit = cur_size;
        }
        m_reserved_bits.set_bit(nbit);
        return nbit;
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
