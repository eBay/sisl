//
// Author: Hari Kadayam
// Created on: Apr 7 2021
//

#pragma once

#include <boost/intrusive/list.hpp>

using namespace boost::intrusive;

namespace sisl {
#pragma pack(1)
class ValueEntryBase {
    static constexpr size_t SIZE_BITS = 29;
    static constexpr size_t PINNED_BITS = 1;
    static constexpr size_t RECORD_FAMILY_ID_BITS = 2;

    struct cache_info {
        uint32_t size : SIZE_BITS;
        uint32_t pinned : PINNED_BITS;
        uint32_t record_family_id : RECORD_FAMILY_ID_BITS;

        cache_info() : size{0}, pinned{0}, record_family_id{0} {}
        void set_pinned(bool is_pinned) { pinned = is_pinned ? 1 : 0; }
        void set_size(uint32_t sz) { size = sz; }
        void set_family_id(uint32_t fid) { record_family_id = fid; }
    };

public:
    mutable list_member_hook< link_mode< auto_unlink > > m_member_hook;
    mutable cache_info m_u;

public:
    ValueEntryBase() = default;
    ValueEntryBase(const ValueEntryBase&) = delete;
    ValueEntryBase& operator=(const ValueEntryBase&) = delete;
    ValueEntryBase(ValueEntryBase&& other) {
        m_u = std::move(other.m_u);
        m_member_hook.swap_nodes(other.m_member_hook);
    }
    ValueEntryBase& operator=(ValueEntryBase&& other) {
        m_u = std::move(other.m_u);
        m_member_hook.swap_nodes(other.m_member_hook);
        return *this;
    }

    void set_size(const uint32_t size) { m_u.size = size; }
    void set_pinned() { m_u.set_pinned(true); }
    void set_unpinned() { m_u.set_pinned(false); }
    void set_record_family(const uint32_t record_fid) { m_u.record_family_id = record_fid; }

    uint32_t size() const { return m_u.size; }
    bool is_pinned() const { return (m_u.pinned == 1); }
    uint32_t record_family_id() const { return m_u.record_family_id; }

    static constexpr size_t max_record_families() { return (1 << RECORD_FAMILY_ID_BITS); }
};
#pragma pack()
} // namespace sisl
