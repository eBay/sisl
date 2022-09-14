/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam, Rishabh Mittal
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

#include <string>
#include <vector>
#include <fmt/format.h>
#include "fds/buffer.hpp"

namespace sisl {
namespace btree {

ENUM(MultiMatchOption, uint16_t,
     DO_NOT_CARE, // Select anything that matches
     LEFT_MOST,   // Select the left most one
     RIGHT_MOST,  // Select the right most one
     MID          // Select the middle one
)

ENUM(btree_put_type, uint16_t,
     INSERT_ONLY_IF_NOT_EXISTS, // Insert
     REPLACE_ONLY_IF_EXISTS,    // Upsert
     REPLACE_IF_EXISTS_ELSE_INSERT,
     APPEND_ONLY_IF_EXISTS, // Update
     APPEND_IF_EXISTS_ELSE_INSERT)

// The base class, btree library expects its key to be derived from
class BtreeKeyRange;
class BtreeKey {
public:
    BtreeKey() = default;

    // Deleting copy constructor forces the derived class to define its own copy constructor
    // BtreeKey(const BtreeKey& other) = delete;
    // BtreeKey(const sisl::blob& b) = delete;
    BtreeKey(const BtreeKey& other) = default;
    virtual ~BtreeKey() = default;

    virtual BtreeKey& operator=(const BtreeKey& other) {
        clone(other);
        return *this;
    };

    virtual void clone(const BtreeKey& other) = 0;
    virtual int compare(const BtreeKey& other) const = 0;

    /* Applicable only for extent keys, so do default compare */
    virtual int compare_head(const BtreeKey& other) const { return compare(other); };

    virtual int compare_range(const BtreeKeyRange& range) const = 0;

    virtual sisl::blob serialize() const = 0;
    virtual uint32_t serialized_size() const = 0;
    // virtual void deserialize(const sisl::blob& b) = 0;

    // Applicable only to extent keys, where keys have head and tail
    virtual sisl::blob serialize_tail() const { return serialize(); }

    virtual std::string to_string() const = 0;
    virtual bool is_extent_key() const { return false; }
};

class BtreeKeyRange {
public:
    const BtreeKey* m_input_start_key{nullptr};
    const BtreeKey* m_input_end_key{nullptr};
    bool m_start_incl;
    bool m_end_incl;
    MultiMatchOption m_multi_selector;

    friend class BtreeSearchState;

    template < typename K >
    friend class BtreeKeyRangeSafe;

    void set_multi_option(MultiMatchOption o) { m_multi_selector = o; }
    virtual const BtreeKey& start_key() const { return *m_input_start_key; }
    virtual const BtreeKey& end_key() const { return *m_input_end_key; }

    virtual bool is_start_inclusive() const { return m_start_incl; }
    virtual bool is_end_inclusive() const { return m_end_incl; }
    virtual bool is_simple_search() const {
        return ((m_input_start_key == m_input_end_key) && (m_start_incl == m_end_incl));
    }
    MultiMatchOption multi_option() const { return m_multi_selector; }

private:
    BtreeKeyRange(const BtreeKey* start_key, bool start_incl, const BtreeKey* end_key, bool end_incl,
                  MultiMatchOption option) :
            m_input_start_key{start_key},
            m_input_end_key{end_key},
            m_start_incl{start_incl},
            m_end_incl{end_incl},
            m_multi_selector{option} {}
    BtreeKeyRange(const BtreeKey* start_key, bool start_incl, MultiMatchOption option) :
            m_input_start_key{start_key},
            m_input_end_key{start_key},
            m_start_incl{start_incl},
            m_end_incl{start_incl},
            m_multi_selector{option} {}
};

/* This type is for keys which is range in itself i.e each key is having its own
 * start() and end().
 */
class ExtentBtreeKey : public BtreeKey {
public:
    ExtentBtreeKey() = default;
    virtual ~ExtentBtreeKey() = default;
    virtual bool is_extent_key() const { return true; }
    virtual int compare_end(const BtreeKey& other) const = 0;
    virtual int compare_start(const BtreeKey& other) const = 0;

    virtual bool preceeds(const BtreeKey& other) const = 0;
    virtual bool succeeds(const BtreeKey& other) const = 0;

    virtual sisl::blob serialize_tail() const override = 0;

    /* we always compare the end key in case of extent */
    virtual int compare(const BtreeKey& other) const override { return (compare_end(other)); }

    /* we always compare the end key in case of extent */
    virtual int compare_range(const BtreeKeyRange& range) const override { return (compare_end(range.end_key())); }
};

class BtreeValue {
public:
    BtreeValue() = default;
    virtual ~BtreeValue() = default;

    // Deleting copy constructor forces the derived class to define its own copy constructor
    BtreeValue(const BtreeValue& other) = delete;

    virtual blob serialize() const = 0;
    virtual uint32_t serialized_size() const = 0;
    virtual void deserialize(const blob& b, bool copy) = 0;
    // virtual void append_blob(const BtreeValue& new_val, BtreeValue& existing_val) = 0;

    // virtual void set_blob_size(uint32_t size) = 0;
    // virtual uint32_t estimate_size_after_append(const BtreeValue& new_val) = 0;

#if 0
    virtual void get_overlap_diff_kvs(BtreeKey* k1, BtreeValue* v1, BtreeKey* k2, BtreeValue* v2, uint32_t param,
                                      diff_read_next_t& to_read,
                                      std::vector< std::pair< BtreeKey, BtreeValue > >& overlap_kvs) {
        LOGINFO("Not Implemented");
    }
#endif

    virtual std::string to_string() const { return ""; }
};

template < typename K >
class BtreeKeyRangeSafe : public BtreeKeyRange {
private:
    const K m_actual_start_key;
    const K m_actual_end_key;

public:
    BtreeKeyRangeSafe(const BtreeKey& start_key) :
            BtreeKeyRange(nullptr, true, nullptr, true, MultiMatchOption::DO_NOT_CARE), m_actual_start_key{start_key} {
        this->m_input_start_key = &m_actual_start_key;
        this->m_input_end_key = &m_actual_start_key;
    }

    virtual ~BtreeKeyRangeSafe() = default;

    BtreeKeyRangeSafe(const BtreeKey& start_key, const BtreeKey& end_key) :
            BtreeKeyRangeSafe(start_key, true, end_key, true) {}

    BtreeKeyRangeSafe(const BtreeKey& start_key, bool start_incl, const BtreeKey& end_key, bool end_incl,
                      MultiMatchOption option = MultiMatchOption::DO_NOT_CARE) :
            BtreeKeyRange(nullptr, start_incl, nullptr, end_incl, option),
            m_actual_start_key{start_key},
            m_actual_end_key{end_key} {
        this->m_input_start_key = &m_actual_start_key;
        this->m_input_end_key = &m_actual_end_key;
    }

    /******************* all functions are constant *************/
    BtreeKeyRangeSafe< K > start_of_range() const {
        return BtreeKeyRangeSafe< K >(start_key(), is_start_inclusive(), multi_option());
    }
    BtreeKeyRangeSafe< K > end_of_range() const {
        return BtreeKeyRangeSafe< K >(end_key(), is_end_inclusive(), multi_option());
    }
};

struct BtreeLockTracker;
struct BtreeQueryCursor {
    std::unique_ptr< BtreeKey > m_last_key;
    std::unique_ptr< BtreeLockTracker > m_locked_nodes;
    BtreeQueryCursor() = default;

    const sisl::blob serialize() const { return m_last_key ? m_last_key->serialize() : sisl::blob{}; };
    virtual std::string to_string() const { return (m_last_key) ? m_last_key->to_string() : "null"; }
};

// This class holds the current state of the search. This is where intermediate search state are stored
// and it is mutated by the do_put and do_query methods. Expect the current_sub_range and cursor to keep
// getting updated on calls.
class BtreeSearchState {
protected:
    const BtreeKeyRange m_input_range;
    BtreeKeyRange m_current_sub_range;
    BtreeQueryCursor* m_cursor{nullptr};

public:
    BtreeSearchState(BtreeKeyRange&& inp_range, BtreeQueryCursor* cur = nullptr) :
            m_input_range(std::move(inp_range)), m_current_sub_range{m_input_range}, m_cursor{cur} {}

    const BtreeQueryCursor* const_cursor() const { return m_cursor; }
    BtreeQueryCursor* cursor() { return m_cursor; }
    void set_cursor(BtreeQueryCursor* cur) { m_cursor = cur; }
    void reset_cursor() { set_cursor(nullptr); }
    bool is_cursor_valid() const { return (m_cursor != nullptr); }

    template < typename K >
    void set_cursor_key(const BtreeKey& end_key) {
        if (!m_cursor) {
            /* no need to set cursor as user doesn't want to keep track of it */
            return;
        }
        m_cursor->m_last_key = std::make_unique< K >(end_key);
    }

    const BtreeKeyRange& input_range() const { return m_input_range; }
    const BtreeKeyRange& current_sub_range() const { return m_current_sub_range; }
    void set_current_sub_range(const BtreeKeyRange& new_sub_range) { m_current_sub_range = new_sub_range; }
    const BtreeKey& next_key() const {
        return (m_cursor && m_cursor->m_last_key) ? *m_cursor->m_last_key : m_input_range.start_key();
    }

#if 0
    template < typename K >
    BtreeKeyRangeSafe< K > next_start_range() const {
        return BtreeKeyRangeSafe< K >(next_key(), is_start_inclusive(), m_input_range.multi_option());
    }

    template < typename K >
    BtreeKeyRangeSafe< K > end_of_range() const {
        return BtreeKeyRangeSafe< K >(m_input_range.end_key(), is_end_inclusive(), m_input_range.multi_option());
    }
#endif

    BtreeKeyRange next_range() const {
        return BtreeKeyRange(&next_key(), is_start_inclusive(), &m_input_range.end_key(), is_end_inclusive(),
                             m_input_range.multi_option());
    }

private:
    bool is_start_inclusive() const {
        if (m_cursor && m_cursor->m_last_key) {
            // cursor always have the last key not included
            return false;
        } else {
            return m_input_range.is_start_inclusive();
        }
    }

    bool is_end_inclusive() const { return m_input_range.is_end_inclusive(); }
};

class BtreeNodeInfo : public BtreeValue {
private:
    bnodeid_t m_bnodeid{empty_bnodeid};

public:
    BtreeNodeInfo() = default;
    explicit BtreeNodeInfo(const bnodeid_t& id) : m_bnodeid(id) {}
    BtreeNodeInfo& operator=(const BtreeNodeInfo& other) = default;

    bnodeid_t bnode_id() const { return m_bnodeid; }
    void set_bnode_id(bnodeid_t bid) { m_bnodeid = bid; }
    bool has_valid_bnode_id() const { return (m_bnodeid != empty_bnodeid); }

    sisl::blob serialize() const override {
        sisl::blob b;
        b.size = sizeof(bnodeid_t);
        b.bytes = uintptr_cast(const_cast< bnodeid_t* >(&m_bnodeid));
        return b;
    }
    uint32_t serialized_size() const override { return sizeof(bnodeid_t); }
    static uint32_t get_fixed_size() { return sizeof(bnodeid_t); }
    std::string to_string() const override { return fmt::format("{}", m_bnodeid); }
    bool operator==(const BtreeNodeInfo& other) const { return (m_bnodeid == other.m_bnodeid); }

    void deserialize(const blob& b, bool copy) override {
        DEBUG_ASSERT_EQ(b.size, sizeof(bnodeid_t), "BtreeNodeInfo deserialize received invalid blob");
        m_bnodeid = *(r_cast< bnodeid_t* >(b.bytes));
    }

    friend std::ostream& operator<<(std::ostream& os, const BtreeNodeInfo& b) {
        os << b.m_bnodeid;
        return os;
    }
};

} // namespace btree
} // namespace sisl
