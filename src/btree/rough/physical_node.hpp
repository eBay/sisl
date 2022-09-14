/*
 * physical_node.hpp
 *
 *  Created on: 16-May-2016
 *      Author: Hari Kadayam
 *
 *  Copyright © 2016 Kadayam, Hari. All rights reserved.
 */
#pragma once

#include <folly/SharedMutex.h>
#include "logging/logging.h"
#include "btree_internal.h"

namespace sisl {
static constexpr uint8_t BTREE_NODE_VERSION = 1;
static constexpr uint8_t BTREE_NODE_MAGIC = 0xab;

#pragma pack(1)
struct persistent_hdr_t {
    uint8_t magic{BTREE_NODE_MAGIC};
    uint8_t version{BTREE_NODE_VERSION};
    uint16_t checksum;

    bnodeid_t node_id;
    bnodeid_t next_node;

    uint32_t nentries : 27;
    uint32_t node_type : 3;
    uint32_t leaf : 1;
    uint32_t valid_node : 1;

    uint64_t node_gen;
    bnodeid_t edge_entry;

    std::string to_string() const {
        return fmt::format("magic={} version={} csum={} node_id={} next_node={} nentries={} node_type={} is_leaf={} "
                           "valid_node={} node_gen={} edge_entry={}",
                           magic, version, checksum, node_id, next_node, nentries, node_type, leaf, valid_node,
                           node_gen, edge_entry);
    }
};
#pragma pack()

#ifndef NO_CHECKSUM
struct verify_result {
    uint8_t act_magic;
    uint16_t act_checksum;
    uint8_t exp_magic;
    uint16_t exp_checksum;

    std::string to_string() const {
        return fmt::format(" Actual magic={} Expected magic={} Actual checksum={} Expected checksum={}", act_magic,
                           exp_magic, act_checksum, exp_checksum);
    }

    friend ostream& operator<<(ostream& os, const verify_result& vr) {
        os << vr.to_string();
        return os;
    }
};
#endif

class BtreeSearchRange;

#pragma pack(1)
template < typename VariantNodeT >
class PhysicalNode {
protected:
    persistent_hdr_t m_pers_header;
    uint8_t m_node_area[0];

public:
    PhysicalNode(bnodeid_t* id, bool init) {
        if (init) {
            set_magic();
            init_checksum();
            set_leaf(true);
            set_total_entries(0);
            set_next_bnode(empty_bnodeid);
            set_gen(0);
            set_valid_node(true);
            set_edge_id(empty_bnodeid);
            set_node_id(*id);
        } else {
            DEBUG_ASSERT_EQ(get_node_id(), *id);
            DEBUG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC);
            DEBUG_ASSERT_EQ(get_version(), BTREE_NODE_VERSION);
        }
    }
    PhysicalNode(bnodeid_t id, bool init) {
        if (init) {
            set_magic();
            init_checksum();
            set_leaf(true);
            set_total_entries(0);
            set_next_bnode(empty_bnodeid);
            set_gen(0);
            set_valid_node(true);
            set_edge_id(empty_bnodeid);
            set_node_id(id);
        } else {
            DEBUG_ASSERT_EQ(get_node_id(), id);
            DEBUG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC);
            DEBUG_ASSERT_EQ(get_version(), BTREE_NODE_VERSION);
        }
    }
    ~PhysicalNode() = default;

    persistent_hdr_t* get_persistent_header() { return &m_pers_header; }

    uint8_t get_magic() const { return m_pers_header.magic; }
    void set_magic() { m_pers_header.magic = BTREE_NODE_MAGIC; }

    uint8_t get_version() const { return m_pers_header.version; }
    uint16_t get_checksum() const { return m_pers_header.checksum; }
    void init_checksum() { m_pers_header.checksum = 0; }

    void set_node_id(bnodeid_t id) { m_pers_header.node_id = id; }
    bnodeid_t get_node_id() const { return m_pers_header.node_id; }

#ifndef NO_CHECKSUM
    void set_checksum(size_t size) { m_pers_header.checksum = crc16_t10dif(init_crc_16, m_node_area, size); }
    bool verify_node(size_t size, verify_result& vr) const {
        HS_DEBUG_ASSERT_EQ(is_valid_node(), true, "verifying invalide node {}!", m_pers_header.to_string());
        vr.act_magic = get_magic();
        vr.exp_magic = BTREE_NODE_MAGIC;
        vr.act_checksum = get_checksum();
        vr.exp_checksum = crc16_t10dif(init_crc_16, m_node_area, size);
        return (vr.act_magic == vr.exp_magic && vr.act_checksum == vr.exp_checksum) ? true : false;
    }
#endif

    uint32_t get_total_entries() const { return m_pers_header.nentries; }
    bool is_leaf() const { return m_pers_header.leaf; }
    btree_node_type get_node_type() const { return s_cast< btree_node_type >(m_pers_header.node_type); }

protected:
    void set_total_entries(uint32_t n) { get_persistent_header()->nentries = n; }
    void inc_entries() { ++get_persistent_header()->nentries; }
    void dec_entries() { --get_persistent_header()->nentries; }

    void add_entries(uint32_t addn) { get_persistent_header()->nentries += addn; }
    void sub_entries(uint32_t subn) { get_persistent_header()->nentries -= subn; }

    void set_leaf(bool leaf) { get_persistent_header()->leaf = leaf; }
    void set_node_type(btree_node_type t) { get_persistent_header()->node_type = uint32_cast(t); }
    uint64_t get_gen() const { return m_pers_header.node_gen; }
    void inc_gen() { get_persistent_header()->node_gen++; }
    void set_gen(uint64_t g) { get_persistent_header()->node_gen = g; }

    void set_valid_node(bool valid) { get_persistent_header()->valid_node = (valid ? 1 : 0); }
    bool is_valid_node() const { return m_pers_header.valid_node; }

    uint8_t* get_node_area_mutable() { return m_node_area; }
    const uint8_t* get_node_area() const { return m_node_area; }

    uint32_t get_occupied_size(const BtreeConfig& cfg) const {
        return (cfg.get_node_area_size() - to_variant_node_const().get_available_size(cfg));
    }
    uint32_t get_suggested_min_size(const BtreeConfig& cfg) const { return cfg.get_max_key_size(); }

    bool is_merge_needed(const BtreeConfig& cfg) const {
#ifdef _PRERELEASE
        if (homestore_flip->test_flip("btree_merge_node") && get_occupied_size(cfg) < cfg.get_node_area_size()) {
            return true;
        }

        auto ret = homestore_flip->get_test_flip< uint64_t >("btree_merge_node_pct");
        if (ret && get_occupied_size(cfg) < (ret.get() * cfg.get_node_area_size() / 100)) { return true; }
#endif
        return (get_occupied_size(cfg) < get_suggested_min_size(cfg));
    }

    bnodeid_t next_bnode() const { return m_pers_header.next_node; }
    void set_next_bnode(bnodeid_t b) { get_persistent_header()->next_node = b; }

    bnodeid_t get_edge_id() const { return m_pers_header.edge_entry; }
    void set_edge_id(bnodeid_t edge) { get_persistent_header()->edge_entry = edge; }

    typedef std::pair< bool, int > node_find_result_t;

    ////////// Top level functions (CRUD on a node) //////////////////
    // Find the slot where the key is present. If not present, return the closest location for the key.
    // Assumption: Node lock is already taken
    node_find_result_t find(const BtreeSearchRange& range, BtreeKey* outkey, BtreeValue* outval, bool copy_key = true,
                            bool copy_val = true) const {
        LOGMSG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC, "Magic mismatch on btree_node {}", m_pers_header.to_string());

        auto result = bsearch_node(range);
        if (result.end_of_search_index == int_cast(get_total_entries()) && !has_valid_edge()) {
            assert(!result.found);
            return result;
        }

        if (get_total_entries() == 0) {
            assert(has_valid_edge() || is_leaf());
            if (is_leaf()) {
                /* Leaf doesn't have any elements */
                return result;
            }
        }

        if (outval) { to_variant_node_const().get(result.end_of_search_index, outval, copy_val /* copy */); }

        if (!range.is_simple_search() && outkey) {
            to_variant_node_const().get_nth_key(result.end_of_search_index, outkey, copy_key /* copy */);
        }

        return result;
    }

    node_find_result_t find(const BtreeKey& find_key, BtreeValue* outval, bool copy_val = true) const {
        return find(BtreeSearchRange(find_key), nullptr, outval, false, copy_val);
    }

    void get_last_key(BtreeKey* out_lastkey) const {
        if (get_total_entries() == 0) { return; }
        to_variant_node().get_nth_key(get_total_entries() - 1, out_lastkey, true);
    }

    void get_first_key(BtreeKey* out_firstkey) const { return to_variant_node().get_nth_key(0, out_firstkey, true); }
    void get_var_nth_key(int i, BtreeKey* out_firstkey) const {
        return to_variant_node().get_nth_key(i, out_firstkey, true);
    }

    uint32_t get_all(const BtreeSearchRange& range, uint32_t max_count, int& start_ind, int& end_ind,
                     std::vector< std::pair< K, V > >* out_values = nullptr) const {
        LOGMSG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC, "Magic mismatch on btree_node {}", m_pers_header.to_string());
        auto count = 0U;

        // Get the start index of the search range.
        BtreeSearchRange sr = range.get_start_of_range();
        sr.set_multi_option(MultiMatchOption::DO_NOT_CARE);

        auto result = bsearch_node(sr); // doing bsearch only based on start key
        // at this point start index will point to exact found or element after that
        start_ind = result.end_of_search_index;

        if (!range.is_start_inclusive()) {
            if (start_ind < (int)get_total_entries()) {
                /* start is not inclusive so increment the start_ind if it is same as this key */
                int x = to_variant_node_const().compare_nth_key(*range.get_start_key(), start_ind);
                if (x == 0) { start_ind++; }
            } else {
                assert(is_leaf() || has_valid_edge());
            }
        }

        if (start_ind == (int)get_total_entries() && is_leaf()) {
            end_ind = start_ind;
            return 0; // no result found
        }

        assert((start_ind < (int)get_total_entries()) || has_valid_edge());

        // search by the end index
        BtreeSearchRange er = range.get_end_of_range();
        er.set_multi_option(MultiMatchOption::DO_NOT_CARE);
        result = bsearch_node(er); // doing bsearch only based on end key
        end_ind = result.end_of_search_index;

        assert(start_ind <= end_ind);

        /* we don't support end exclusive */
        assert(range.is_end_inclusive());

        if (end_ind == (int)get_total_entries() && !has_valid_edge()) { --end_ind; }

        if (is_leaf()) {
            /* Decrement the end indx if range doesn't overlap with the start of key at end indx */
            sisl::blob blob;
            K key;
            to_variant_node().get_nth_key(end_ind, &key, false);

            if ((range.get_start_key())->compare_start(&key) < 0 && ((range.get_end_key())->compare_start(&key)) < 0) {
                if (start_ind == end_ind) {
                    /* no match */
                    return 0;
                }
                --end_ind;
            }
        }

        assert(start_ind <= end_ind);
        count = end_ind - start_ind + 1;
        if (count > max_count) { count = max_count; }

        /* We should always find the entries in interior node */
        assert(start_ind < (int)get_total_entries() || has_valid_edge());
        assert(end_ind < (int)get_total_entries() || has_valid_edge());

        if (out_values == nullptr) { return count; }

        /* get the keys and values */
        for (auto i = start_ind; i < (int)(start_ind + count); ++i) {
            K key;
            V value;
            if (i == (int)get_total_entries() && !is_leaf())
                get_edge_value(&value); // invalid key in case of edge entry for internal node
            else {
                to_variant_node().get_nth_key(i, &key, true);
                to_variant_node().get_nth_value(i, &value, true);
            }
            out_values->emplace_back(std::make_pair<>(key, value));
        }
        return count;
    }

    bool put(const BtreeKey& key, const BtreeValue& val, btree_put_type put_type, BtreeValue& existing_val) {
        DEBUG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC);
        auto result = find(key, nullptr, nullptr);
        bool ret = true;

        LOGMSG_ASSERT((get_magic() == BTREE_NODE_MAGIC), "{}", m_pers_header.to_string());
        if (put_type == btree_put_type::INSERT_ONLY_IF_NOT_EXISTS) {
            if (result.found) {
                LOGINFO("entry already exist");
                return false;
            }
            (void)to_variant_node().insert(result.end_of_search_index, key, val);
        } else if (put_type == btree_put_type::REPLACE_ONLY_IF_EXISTS) {
            if (!result.found) return false;
            to_variant_node().update(result.end_of_search_index, key, val);
        } else if (put_type == btree_put_type::REPLACE_IF_EXISTS_ELSE_INSERT) {
            !(result.found) ? (void)to_variant_node().insert(result.end_of_search_index, key, val)
                            : to_variant_node().update(result.end_of_search_index, key, val);
        } else if (put_type == btree_put_type::APPEND_ONLY_IF_EXISTS) {
            if (!result.found) return false;
            append(result.end_of_search_index, key, val, existing_val);
        } else if (put_type == btree_put_type::APPEND_IF_EXISTS_ELSE_INSERT) {
            (!result.found) ? (void)to_variant_node().insert(result.end_of_search_index, key, val)
                            : append(result.end_of_search_index, key, val, existing_val);
        } else {
            assert(false);
        }
        DEBUG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC);

        LOGMSG_ASSERT((get_magic() == BTREE_NODE_MAGIC), "{}", m_pers_header.to_string());
        return ret;
    }

    btree_status_t insert(const BtreeKey& key, const BtreeValue& val) {
        auto result = find(key, nullptr, nullptr);
        assert(!is_leaf() || (!result.found)); // We do not support duplicate keys yet
        auto ret = to_variant_node().insert(result.end_of_search_index, key, val);
        DEBUG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC);
        return ret;
    }

    bool remove_one(const BtreeSearchRange& range, BtreeKey* outkey, BtreeValue* outval) {
        auto result = find(range, outkey, outval);
        if (!result.found) { return false; }

        to_variant_node().remove(result.end_of_search_index);
        LOGMSG_ASSERT((get_magic() == BTREE_NODE_MAGIC), "{}", m_pers_header.to_string());
        return true;
    }

    void append(uint32_t index, const BtreeKey& key, const BtreeValue& val, BtreeValue& existing_val) {
        // Get the nth value and do a callback to update its blob with the new value, being passed
        V nth_val;
        to_variant_node().get_nth_value(index, &nth_val, false);
        nth_val.append_blob(val, existing_val);
        to_variant_node().update(index, key, nth_val);
        DEBUG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC);
    }

    /* Update the key and value pair and after update if outkey and outval are non-nullptr, it fills them with
     * the key and value it just updated respectively */
    void update(const BtreeKey& key, const BtreeValue& val, BtreeKey* outkey, BtreeValue* outval) {
        auto result = find(key, outkey, outval);
        assert(result.found);
        to_variant_node().update(result.end_of_search_index, val);
        LOGMSG_ASSERT((get_magic() == BTREE_NODE_MAGIC), "{}", m_pers_header.to_string());
    }

    //////////// Edge Related Methods ///////////////
    void invalidate_edge() { set_edge_id(empty_bnodeid); }

    void set_edge_value(const BtreeValue& v) {
        BtreeNodeInfo* bni = (BtreeNodeInfo*)&v;
        set_edge_id(bni->bnode_id());
        DEBUG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC);
    }

    void get_edge_value(BtreeValue* v) const {
        if (is_leaf()) { return; }
        v->set_blob(BtreeNodeInfo(get_edge_id()).get_blob());
    }

    bool has_valid_edge() const {
        if (is_leaf()) { return false; }
        return (get_edge_id() != empty_bnodeid);
    }

    void get_adjacent_indicies(uint32_t cur_ind, vector< int >& indices_list, uint32_t max_indices) const {
        uint32_t i = 0;
        uint32_t start_ind;
        uint32_t end_ind;
        uint32_t nentries = this->get_total_entries();

        auto max_ind = ((max_indices / 2) - 1 + (max_indices % 2));
        end_ind = cur_ind + (max_indices / 2);
        if (cur_ind < max_ind) {
            end_ind += max_ind - cur_ind;
            start_ind = 0;
        } else {
            start_ind = cur_ind - max_ind;
        }

        for (i = start_ind; (i <= end_ind) && (indices_list.size() < max_indices); i++) {
            if (i == nentries) {
                if (this->has_valid_edge()) { indices_list.push_back(i); }
                break;
            } else {
                indices_list.push_back(i);
            }
        }
    }

protected:
    node_find_result_t bsearch_node(const BtreeSearchRange& range) const {
        DEBUG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC);
        const auto ret = bsearch(-1, get_total_entries(), range);
        const auto selection = range.multi_option();

        if (ret.found) { assert(ret.end_of_search_index < (int)get_total_entries() && ret.end_of_search_index > -1); }

        /* BEST_FIT_TO_CLOSEST is used by remove only. Remove doesn't support range_remove. Until
         * then we have the special logic :
         */
        if (selection == MultiMatchOption::BEST_FIT_TO_CLOSEST_FOR_REMOVE) {
            if (!ret.found && is_leaf()) {
                if (get_total_entries() != 0) {
                    ret.end_of_search_index = get_total_entries() - 1;
                    ret.found = true;
                }
            }
        }

        return ret;
    }

    node_find_result_t is_bsearch_left_or_right_most(const BtreeSearchRange& range) const {
        auto selection = range.multi_option();
        if (range.is_simple_search()) { return (MultiMatchOption::DO_NOT_CARE); }
        if (selection == MultiMatchOption::LEFT_MOST) {
            return (MultiMatchOption::LEFT_MOST);
        } else if (selection == MultiMatchOption::RIGHT_MOST) {
            return (MultiMatchOption::RIGHT_MOST);
        } else if (selection == MultiMatchOption::BEST_FIT_TO_CLOSEST_FOR_REMOVE) {
            return (MultiMatchOption::LEFT_MOST);
        }
        return (MultiMatchOption::DO_NOT_CARE);
    }

    /* This function does bseach between start and end where start and end are not included.
     * It either gives left most, right most or the first found entry based on the range selection policy.
     * If entry doesn't found then it gives the closest found entry.
     */
    node_find_result_t bsearch(int start, int end, const BtreeSearchRange& range) const {
        int mid = 0;
        int initial_end = end;
        int min_ind_found = INT32_MAX;
        int second_min = INT32_MAX;
        int max_ind_found = 0;

        struct {
            bool found;
            int end_of_search_index;
        } ret{false, 0};

        if ((end - start) <= 1) { return ret; }

        auto selection = is_bsearch_left_or_right_most(range);

        while ((end - start) > 1) {
            mid = start + (end - start) / 2;
            assert(mid >= 0 && mid < (int)get_total_entries());
            int x = range.is_simple_search() ? to_variant_node_const().compare_nth_key(*range.get_start_key(), mid)
                                             : to_variant_node_const().compare_nth_key_range(range, mid);
            if (x == 0) {
                ret.found = true;
                if (selection == MultiMatchOption::DO_NOT_CARE) {
                    end = mid;
                    break;
                } else if (selection == MultiMatchOption::LEFT_MOST) {
                    if (mid < min_ind_found) { min_ind_found = mid; }
                    end = mid;
                } else if (selection == MultiMatchOption::RIGHT_MOST) {
                    if (mid > max_ind_found) { max_ind_found = mid; }
                    start = mid;
                } else {
                    assert(false);
                }
            } else if (x > 0) {
                end = mid;
            } else {
                start = mid;
            }
        }

        if (ret.found) {
            if (selection == MultiMatchOption::LEFT_MOST) {
                assert(min_ind_found != INT32_MAX);
                ret.end_of_search_index = min_ind_found;
            } else if (selection == MultiMatchOption::RIGHT_MOST) {
                assert(max_ind_found != INT32_MAX);
                ret.end_of_search_index = max_ind_found;
            } else {
                ret.end_of_search_index = end;
            }
        } else {
            ret.end_of_search_index = end;
        }
        return ret;
    }

    VariantNodeT& to_variant_node() { return s_cast< VariantNodeT& >(*this); }
    const VariantNodeT& to_variant_node_const() const { return s_cast< const VariantNodeT& >(*this); }
};
#pragma pack()

} // namespace sisl
