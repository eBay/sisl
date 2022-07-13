/*
 * btree_kv.hpp
 *
 *  Created on: 14-May-2016
 *      Author: Hari Kadayam
 *
 *  Copyright © 2016 Kadayam, Hari. All rights reserved.
 */

#pragma once

#include <string>
#include <vector>
#include <fmt/format.h>
#include "fds/buffer.hpp"

namespace sisl {
namespace btree {

ENUM(_MultiMatchSelector, uint16_t,
     DO_NOT_CARE,                   // Select anything that matches
     LEFT_MOST,                     // Select the left most one
     RIGHT_MOST,                    // Select the right most one
     BEST_FIT_TO_CLOSEST,           // Return the entry either same or more then the search key. If
                                    // nothing is available then return the entry just smaller then the
                                    // search key.
     BEST_FIT_TO_CLOSEST_FOR_REMOVE // It is similar as BEST_FIT_TO_CLOSEST but have special
                                    // handling for remove This code will be removed once
                                    // range query is supported in remove
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
    // BtreeKey(const BtreeKey& other) = delete; // Deleting copy constructor forces the
    // derived class to define its own copy constructor
    virtual ~BtreeKey() = default;

    // virtual BtreeKey& operator=(const BtreeKey& other) = delete; // Deleting = overload forces the derived to
    // define its = overload
    virtual int compare(const BtreeKey& other) const = 0;

    /* Applicable only for extent keys. It compare start key of (*other) with end key of (*this) */
    virtual int compare_start(const BtreeKey& other) const { return compare(other); };
    virtual int compare_range(const BtreeKeyRange& range) const = 0;
    virtual sisl::blob serialize() const = 0;

    /* Applicable to extent keys. It doesn't copy the entire blob. Copy only the end key of the blob */
    // virtual void copy_end_key_blob(const sisl::blob& b) { copy_blob(b); };

    virtual uint32_t serialized_size() const = 0;

    virtual std::string to_string() const = 0;
    virtual bool is_extent_key() { return false; }
};

class BtreeKeyRange {
private:
    const BtreeKey& m_start_key;
    const BtreeKey& m_end_key;
    bool m_start_incl;
    bool m_end_incl;
    _MultiMatchSelector m_multi_selector;

public:
    BtreeKeyRange(const BtreeKey& start_key) :
            BtreeKeyRange(start_key, true, start_key, true, _MultiMatchSelector::DO_NOT_CARE) {}
    BtreeKeyRange(const BtreeKey& start_key, const BtreeKey& end_key) :
            BtreeKeyRange(start_key, true, end_key, true, _MultiMatchSelector::DO_NOT_CARE) {}
    BtreeKeyRange(const BtreeKey& start_key, bool start_incl, _MultiMatchSelector option) :
            BtreeKeyRange(start_key, start_incl, start_key, start_incl, option) {}
    BtreeKeyRange(const BtreeKey& start_key, bool start_incl, const BtreeKey& end_key, bool end_incl) :
            BtreeKeyRange(start_key, start_incl, end_key, end_incl, _MultiMatchSelector::DO_NOT_CARE) {}
    BtreeKeyRange(const BtreeKey& start_key, bool start_incl, const BtreeKey& end_key, bool end_incl,
                  _MultiMatchSelector option) :
            m_start_key{start_key},
            m_end_key{end_key},
            m_start_incl{start_incl},
            m_end_incl{end_incl},
            m_multi_selector{option} {}

    void set_selection_option(_MultiMatchSelector o) { m_multi_selector = o; }

    virtual const BtreeKey& start_key() const { return m_start_key; }
    virtual const BtreeKey& end_key() const { return m_end_key; }
    BtreeKeyRange start_of_range() const { return BtreeKeyRange(start_key(), is_start_inclusive(), m_multi_selector); }
    BtreeKeyRange end_of_range() const { return BtreeKeyRange(end_key(), is_end_inclusive(), m_multi_selector); }
    virtual bool is_start_inclusive() const { return m_start_incl; }
    virtual bool is_end_inclusive() const { return m_end_incl; }
    virtual bool is_simple_search() const { return ((&start_key() == &end_key()) && (m_start_incl == m_end_incl)); }
    _MultiMatchSelector selection_option() const { return m_multi_selector; }
};

/* This type is for keys which is range in itself i.e each key is having its own
 * start() and end().
 */
class ExtentBtreeKey : public BtreeKey {
public:
    ExtentBtreeKey() = default;
    virtual ~ExtentBtreeKey() = default;
    virtual bool is_extent_key() { return true; }
    virtual int compare_end(const BtreeKey& other) const = 0;
    virtual int compare_start(const BtreeKey& other) const override = 0;

    virtual bool preceeds(const BtreeKey& other) const = 0;
    virtual bool succeeds(const BtreeKey& other) const = 0;

    // virtual void copy_end_key_blob(const sisl::blob& b) override = 0;

    /* we always compare the end key in case of extent */
    virtual int compare(const BtreeKey& other) const override { return (compare_end(other)); }

    /* we always compare the end key in case of extent */
    virtual int compare_range(const BtreeKeyRange& range) const override { return (compare_end(range.end_key())); }
};

class BtreeValue {
public:
    BtreeValue() {}
    virtual ~BtreeValue() {}

    // BtreeValue(const BtreeValue& other) = delete; // Deleting copy constructor forces the derived class to define
    // its own copy constructor

    virtual sisl::blob serialize() const = 0;
    virtual uint32_t serialized_size() const = 0;
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
} // namespace btree
} // namespace sisl
