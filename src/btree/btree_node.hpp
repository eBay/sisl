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
#include <iostream>

#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wattributes"
#endif
#include <folly/SharedMutex.h>
#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic pop
#endif

#include "utility/atomic_counter.hpp"
#include "utility/enum.hpp"
#include "utility/obj_life_counter.hpp"
#include "btree_internal.hpp"
#include "btree_kv.hpp"

namespace sisl {
namespace btree {
#pragma pack(1)
struct transient_hdr_t {
    folly::SharedMutexReadPriority lock;
    sisl::atomic_counter< uint16_t > upgraders{0};

    /* these variables are accessed without taking lock and are not expected to change after init */
    uint8_t is_leaf_node{0};
    btree_store_type store_type{btree_store_type::MEM};

#ifndef NDEBUG
    int is_lock{-1};
#endif

    bool is_leaf() const { return (is_leaf_node != 0); }
};
#pragma pack()

struct verify_result {
    uint8_t act_magic;
    uint16_t act_checksum;
    uint8_t exp_magic;
    uint16_t exp_checksum;

    std::string to_string() const {
        return fmt::format("Magic [Expected={} Actual={}] Checksum[Expected={} Actual={}]", exp_magic, act_magic,
                           exp_checksum, act_checksum);
    }

    friend std::ostream& operator<<(std::ostream& os, const verify_result& vr) {
        os << vr.to_string();
        return os;
    }
};

static constexpr uint8_t BTREE_NODE_VERSION = 1;
static constexpr uint8_t BTREE_NODE_MAGIC = 0xab;
ENUM(btree_node_type, uint32_t, SIMPLE, VAR_VALUE, VAR_KEY, VAR_OBJECT, PREFIX, COMPACT)

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

template < typename K, typename V >
class BtreeNode : public sisl::ObjLifeCounter< BtreeNode< K, V > > {
    typedef std::pair< bool, uint32_t > node_find_result_t;

protected:
    atomic_counter< int32_t > m_refcount{0};
    transient_hdr_t m_trans_hdr;
    uint8_t* m_phys_node_buf;

public:
    BtreeNode(uint8_t* node_buf, bnodeid_t id, bool init_buf, bool is_leaf) : m_phys_node_buf{node_buf} {
        if (init_buf) {
            set_magic();
            init_checksum();
            set_leaf(is_leaf);
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
        m_trans_hdr.is_leaf_node = is_leaf;
    }
    virtual ~BtreeNode() = default;

    node_find_result_t find(const BtreeKeyRange& range, K* outkey, V* outval, bool copy_key, bool copy_val) const {
        LOGMSG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC, "Magic mismatch on btree_node {}",
                         get_persistent_header_const()->to_string());

        auto [found, idx] = bsearch_node(range);
        if (idx == get_total_entries() && !has_valid_edge()) {
            DEBUG_ASSERT_EQ(found, false);
            return std::make_pair(found, idx);
        }

        if (get_total_entries() == 0) {
            DEBUG_ASSERT((has_valid_edge() || is_leaf()), "Invalid node");
            if (is_leaf()) { return std::make_pair(found, idx); /* Leaf doesn't have any elements */ }
        }

        if (outval) { *outval = get(idx, copy_val /* copy */); }
        if (outkey) { *outkey = get_nth_key(idx, copy_key /* copy */); }
        return std::make_pair(found, idx);
    }

    node_find_result_t find(const BtreeKey& find_key, V* outval, bool copy_val) const {
        return find(BtreeKeyRange(find_key), nullptr, outval, false, copy_val);
    }

    uint32_t get_all(const BtreeKeyRange& range, uint32_t max_count, uint32_t& start_ind, uint32_t& end_ind,
                     std::vector< std::pair< K, V > >* out_values = nullptr) const {
        LOGMSG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC, "Magic mismatch on btree_node {}",
                         get_persistent_header_const()->to_string());
        auto count = 0U;

        // Get the start index of the search range.
        BtreeKeyRange sr = range.start_of_range();
        sr.set_selection_option(_MultiMatchSelector::DO_NOT_CARE);
        const auto [sfound, sind] = bsearch_node(sr); // doing bsearch only based on start key
        // at this point start index will point to exact found or element after that
        start_ind = sind;

        if (!range.is_start_inclusive()) {
            if (start_ind < get_total_entries()) {
                /* start is not inclusive so increment the start_ind if it is same as this key */
                const int x = compare_nth_key(range.start_key(), start_ind);
                if (x == 0) { ++start_ind; }
            } else {
                DEBUG_ASSERT(is_leaf() || has_valid_edge(), "Invalid node");
            }
        }

        if (start_ind == get_total_entries() && is_leaf()) {
            end_ind = start_ind;
            return 0; // no result found
        }
        DEBUG_ASSERT((start_ind < get_total_entries()) || has_valid_edge(), "Invalid node");

        // search by the end index
        BtreeKeyRange er = range.end_of_range();
        er.set_selection_option(_MultiMatchSelector::DO_NOT_CARE);
        const auto [efound, eind] = bsearch_node(er); // doing bsearch only based on end key
        end_ind = eind;

        if (end_ind == get_total_entries() && !has_valid_edge()) { --end_ind; }
        if (is_leaf()) {
            /* Decrement the end indx if range doesn't overlap with the start of key at end indx */
            K key = get_nth_key(end_ind, false);
            if ((range.start_key().compare_start(key) < 0) && ((range.end_key().compare_start(key)) < 0)) {
                if (start_ind == end_ind) { return 0; /* no match */ }
                --end_ind;
            }
        }

        /* We should always find the entries in interior node */
        DEBUG_ASSERT_LE(start_ind, end_ind);
        // DEBUG_ASSERT_EQ(range.is_end_inclusive(), true); /* we don't support end exclusive */
        DEBUG_ASSERT(start_ind < get_total_entries() || has_valid_edge(), "Invalid node");

        count = std::min(end_ind - start_ind + 1, max_count);
        if (out_values == nullptr) { return count; }

        /* get the keys and values */
        for (auto i{start_ind}; i < (start_ind + count); ++i) {
            if (i == get_total_entries() && !is_leaf()) {
                // invalid key in case of edge entry for internal node
                out_values->emplace_back(std::make_pair(K{}, get_edge_value()));
            } else {
                out_values->emplace_back(std::make_pair(get_nth_key(i, true), get_nth_value(i, true)));
            }
        }
        return count;
    }

    bool put(const BtreeKey& key, const BtreeValue& val, btree_put_type put_type, V* existing_val) {
        LOGMSG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC, "Magic mismatch on btree_node {}",
                         get_persistent_header_const()->to_string());
        bool ret = true;

        const auto [found, idx] = find(key, nullptr, false);
        if (found && existing_val) { *existing_val = get(idx, true); }

        if (put_type == btree_put_type::INSERT_ONLY_IF_NOT_EXISTS) {
            if (found) {
                LOGDEBUG("Attempt to insert duplicate entry {}", key.to_string());
                return false;
            }
            insert(idx, key, val);
        } else if (put_type == btree_put_type::REPLACE_ONLY_IF_EXISTS) {
            if (!found) return false;
            update(idx, key, val);
        } else if (put_type == btree_put_type::REPLACE_IF_EXISTS_ELSE_INSERT) {
            (found) ? update(idx, key, val) : insert(idx, key, val);
        } else if (put_type == btree_put_type::APPEND_ONLY_IF_EXISTS) {
            if (!found) return false;
            append(idx, key, val);
        } else if (put_type == btree_put_type::APPEND_IF_EXISTS_ELSE_INSERT) {
            (found) ? append(idx, key, val) : insert(idx, key, val);
        } else {
            DEBUG_ASSERT(false, "Wrong put_type {}", put_type);
        }
        return ret;
    }

    virtual btree_status_t insert(const BtreeKey& key, const BtreeValue& val) {
        const auto [found, idx] = find(key, nullptr, false);
        DEBUG_ASSERT(!is_leaf() || (!found), "Invalid node"); // We do not support duplicate keys yet
        insert(idx, key, val);
        DEBUG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC, "{}", get_persistent_header_const()->to_string());
        return btree_status_t::success;
    }

    virtual bool remove_one(const BtreeKeyRange& range, K* outkey, V* outval) {
        const auto [found, idx] = find(range, outkey, outval, true, true);
        if (!found) { return false; }
        remove(idx);
        LOGMSG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC, "{}", get_persistent_header_const()->to_string());
        return true;
    }

    /* Update the key and value pair and after update if outkey and outval are non-nullptr, it fills them with
     * the key and value it just updated respectively */
    virtual bool update_one(const BtreeKey& key, const BtreeValue& val, K* outkey, V* outval) {
        const auto [found, idx] = find(key, outkey, outval, true, true);
        if (!found) { return false; }
        update(idx, val);
        LOGMSG_ASSERT((get_magic() == BTREE_NODE_MAGIC), "{}", get_persistent_header_const()->to_string());
        return true;
    }

    void get_adjacent_indicies(uint32_t cur_ind, std::vector< uint32_t >& indices_list, uint32_t max_indices) const {
        uint32_t i = 0;
        uint32_t start_ind;
        uint32_t end_ind;
        uint32_t nentries = get_total_entries();

        auto max_ind = ((max_indices / 2) - 1 + (max_indices % 2));
        end_ind = cur_ind + (max_indices / 2);
        if (cur_ind < max_ind) {
            end_ind += max_ind - cur_ind;
            start_ind = 0;
        } else {
            start_ind = cur_ind - max_ind;
        }

        for (i = start_ind; (i <= end_ind) && (indices_list.size() < max_indices); ++i) {
            if (i == nentries) {
                if (has_valid_edge()) { indices_list.push_back(i); }
                break;
            } else {
                indices_list.push_back(i);
            }
        }
    }

    std::tuple< K, bool, K, bool > get_subrange(const BtreeKeyRange& inp_range, int upto_ind) const {
#ifndef NDEBUG
        if (upto_ind > 0) {
            /* start of input range should always be more then the key in curr_ind - 1 */
            DEBUG_ASSERT_LE(get_nth_key(upto_ind - 1, false).compare(inp_range.start_key()), 0, "[node={}]",
                            to_string());
        }
#endif

        // find end of subrange
        bool end_inc = true;
        K end_key;

        if (upto_ind < int_cast(get_total_entries())) {
            end_key = get_nth_key(upto_ind, false);
            if (end_key.compare(inp_range.end_key()) >= 0) {
                /* this is last index to process as end of range is smaller then key in this node */
                end_key = inp_range.end_key();
                end_inc = inp_range.is_end_inclusive();
            } else {
                end_inc = true;
            }
        } else {
            /* it is the edge node. end key is the end of input range */
            LOGMSG_ASSERT_EQ(has_valid_edge(), true, "node={}", to_string());
            end_key = inp_range.end_key();
            end_inc = inp_range.is_end_inclusive();
        }

        auto subrange = std::make_tuple(K{inp_range.start_key().serialize(), true}, inp_range.is_start_inclusive(),
                                        K{end_key.serialize(), true}, end_inc);
        RELEASE_ASSERT_LE(std::get< 0 >(subrange).compare(std::get< 3 >(subrange)), 0, "[node={}]", to_string());
        RELEASE_ASSERT_LE(std::get< 0 >(subrange).compare(inp_range.end_key()), 0, "[node={}]", to_string());
        return subrange;
    }

    K get_last_key() const {
        if (get_total_entries() == 0) { return K{}; }
        return get_nth_key(get_total_entries() - 1, true);
    }

    K get_first_key() const { return get_nth_key(0, true); }

    bool validate_key_order() const {
        for (auto i = 1u; i < get_total_entries(); ++i) {
            auto prevKey = get_nth_key(i - 1, false);
            auto curKey = get_nth_key(i, false);
            if (prevKey.compare(curKey) >= 0) {
                DEBUG_ASSERT(false, "Order check failed at entry={}", i);
                return false;
            }
        }
        return true;
    }

    uint32_t get_total_entries() const { return get_persistent_header_const()->nentries; }

public:
    // Public method which needs to be implemented by variants
    virtual uint32_t move_out_to_right_by_entries(const BtreeConfig& cfg, BtreeNode< K, V >& other_node,
                                                  uint32_t nentries) = 0;
    virtual uint32_t move_out_to_right_by_size(const BtreeConfig& cfg, BtreeNode< K, V >& other_node,
                                               uint32_t size) = 0;
    virtual uint32_t move_in_from_right_by_entries(const BtreeConfig& cfg, BtreeNode< K, V >& other_node,
                                                   uint32_t nentries) = 0;
    virtual uint32_t move_in_from_right_by_size(const BtreeConfig& cfg, BtreeNode< K, V >& other_node,
                                                uint32_t size) = 0;
    virtual uint32_t get_available_size(const BtreeConfig& cfg) const = 0;
    virtual K get_nth_key(uint32_t ind, bool copykey) const = 0;
    virtual V get_nth_value(uint32_t ind, bool copy) const = 0;
    virtual std::string to_string(bool print_friendly = false) const = 0;

protected:
    virtual void insert(uint32_t ind, const BtreeKey& key, const BtreeValue& val) = 0;
    virtual V get(uint32_t ind, bool copy) const = 0;
    virtual void remove(uint32_t ind) { remove(ind, ind); }
    virtual void remove(uint32_t ind_s, uint32_t ind_e) = 0;
    virtual void update(uint32_t ind, const BtreeValue& val) = 0;
    virtual void update(uint32_t ind, const BtreeKey& key, const BtreeValue& val) = 0;
    virtual void append(uint32_t ind, const BtreeKey& key, const BtreeValue& val) = 0;

    virtual uint32_t get_nth_obj_size(uint32_t ind) const = 0;
    virtual int compare_nth_key(const BtreeKey& cmp_key, uint32_t ind) const = 0;
    virtual int compare_nth_key_range(const BtreeKeyRange& range, uint32_t ind) const = 0;

protected:
    persistent_hdr_t* get_persistent_header() { return r_cast< persistent_hdr_t* >(m_phys_node_buf); }
    const persistent_hdr_t* get_persistent_header_const() const {
        return r_cast< const persistent_hdr_t* >(m_phys_node_buf);
    }
    uint8_t* node_data_area() { return (m_phys_node_buf + sizeof(persistent_hdr_t)); }
    const uint8_t* node_data_area_const() const { return (m_phys_node_buf + sizeof(persistent_hdr_t)); }

    uint8_t get_magic() const { return get_persistent_header_const()->magic; }
    void set_magic() { get_persistent_header()->magic = BTREE_NODE_MAGIC; }

    uint8_t get_version() const { return get_persistent_header_const()->version; }
    uint16_t get_checksum() const { return get_persistent_header_const()->checksum; }
    void init_checksum() { get_persistent_header()->checksum = 0; }

    void set_node_id(bnodeid_t id) { get_persistent_header()->node_id = id; }
    bnodeid_t get_node_id() const { return get_persistent_header_const()->node_id; }

#ifndef NO_CHECKSUM
    void set_checksum(size_t size) {
        get_persistent_header()->checksum = crc16_t10dif(init_crc_16, node_data_area_const(), size);
    }

    bool verify_node(size_t size, verify_result& vr) const {
        HS_DEBUG_ASSERT_EQ(is_valid_node(), true, "verifying invalide node {}!",
                           get_persistent_header_const()->to_string());
        vr.act_magic = get_magic();
        vr.exp_magic = BTREE_NODE_MAGIC;
        vr.act_checksum = get_checksum();
        vr.exp_checksum = crc16_t10dif(init_crc_16, node_data_area_const(), size);
        return (vr.act_magic == vr.exp_magic && vr.act_checksum == vr.exp_checksum) ? true : false;
    }
#endif

    bool is_leaf() const { return get_persistent_header_const()->leaf; }
    btree_node_type get_node_type() const {
        return s_cast< btree_node_type >(get_persistent_header_const()->node_type);
    }

    void set_total_entries(uint32_t n) { get_persistent_header()->nentries = n; }
    void inc_entries() { ++get_persistent_header()->nentries; }
    void dec_entries() { --get_persistent_header()->nentries; }

    void add_entries(uint32_t addn) { get_persistent_header()->nentries += addn; }
    void sub_entries(uint32_t subn) { get_persistent_header()->nentries -= subn; }

    void set_leaf(bool leaf) { get_persistent_header()->leaf = leaf; }
    void set_node_type(btree_node_type t) { get_persistent_header()->node_type = uint32_cast(t); }
    uint64_t get_gen() const { return get_persistent_header_const()->node_gen; }
    void inc_gen() { get_persistent_header()->node_gen++; }
    void set_gen(uint64_t g) { get_persistent_header()->node_gen = g; }

    void set_valid_node(bool valid) { get_persistent_header()->valid_node = (valid ? 1 : 0); }
    bool is_valid_node() const { return get_persistent_header_const()->valid_node; }

    uint32_t get_occupied_size(const BtreeConfig& cfg) const {
        return (cfg.node_area_size() - get_available_size(cfg));
    }
    uint32_t get_suggested_min_size(const BtreeConfig& cfg) const { return cfg.max_key_size(); }

    bool is_merge_needed(const BtreeConfig& cfg) const {
#ifdef _PRERELEASE
        if (homestore_flip->test_flip("btree_merge_node") && get_occupied_size(cfg) < cfg.node_area_size()) {
            return true;
        }

        auto ret = homestore_flip->get_test_flip< uint64_t >("btree_merge_node_pct");
        if (ret && get_occupied_size(cfg) < (ret.get() * cfg.node_area_size() / 100)) { return true; }
#endif
        return (get_occupied_size(cfg) < get_suggested_min_size(cfg));
    }

    bnodeid_t get_next_bnode() const { return get_persistent_header_const()->next_node; }
    void set_next_bnode(bnodeid_t b) { get_persistent_header()->next_node = b; }

    bnodeid_t get_edge_id() const { return get_persistent_header_const()->edge_entry; }
    void set_edge_id(bnodeid_t edge) { get_persistent_header()->edge_entry = edge; }

    bool has_valid_edge() const {
        if (is_leaf()) { return false; }
        return (get_edge_id() != empty_bnodeid);
    }
    virtual V get_edge_value() const { return V{get_edge_id()}; }
    virtual void set_edge_value(const V& v) {}

    void invalidate_edge() { set_edge_id(empty_bnodeid); }

private:
    node_find_result_t bsearch_node(const BtreeKeyRange& range) const {
        DEBUG_ASSERT_EQ(get_magic(), BTREE_NODE_MAGIC);
        auto [found, idx] = bsearch(-1, get_total_entries(), range);
        if (found) { DEBUG_ASSERT_LT(idx, get_total_entries()); }

        /* BEST_FIT_TO_CLOSEST is used by remove only. Remove doesn't support range_remove. Until
         * then we have the special logic :
         */
        if (range.selection_option() == _MultiMatchSelector::BEST_FIT_TO_CLOSEST_FOR_REMOVE) {
            if (!found && is_leaf()) {
                if (get_total_entries() != 0) {
                    idx = get_total_entries() - 1;
                    found = true;
                }
            }
        }

        return std::make_pair(found, idx);
    }

    node_find_result_t bsearch(int start, int end, const BtreeKeyRange& range) const {
        int mid = 0;
        int min_ind_found = INT32_MAX;
        int max_ind_found = 0;

        bool found{false};
        uint32_t end_of_search_index{0};
        if ((end - start) <= 1) { return std::make_pair(found, end_of_search_index); }
        const auto selection = is_bsearch_left_or_right_most(range);

        while ((end - start) > 1) {
            mid = start + (end - start) / 2;
            DEBUG_ASSERT(mid >= 0 && mid < int_cast(get_total_entries()), "Invalid mid={}", mid);
            int x =
                range.is_simple_search() ? compare_nth_key(range.start_key(), mid) : compare_nth_key_range(range, mid);
            if (x == 0) {
                found = true;
                if (selection == _MultiMatchSelector::DO_NOT_CARE) {
                    end = mid;
                    break;
                } else if (selection == _MultiMatchSelector::LEFT_MOST) {
                    if (mid < min_ind_found) { min_ind_found = mid; }
                    end = mid;
                } else if (selection == _MultiMatchSelector::RIGHT_MOST) {
                    if (mid > max_ind_found) { max_ind_found = mid; }
                    start = mid;
                } else {
                    DEBUG_ASSERT(false, "Invalid MatchSelector={}", enum_name(selection));
                }
            } else if (x > 0) {
                end = mid;
            } else {
                start = mid;
            }
        }

        if (found) {
            if (selection == _MultiMatchSelector::LEFT_MOST) {
                DEBUG_ASSERT_NE(min_ind_found, INT32_MAX);
                end_of_search_index = min_ind_found;
            } else if (selection == _MultiMatchSelector::RIGHT_MOST) {
                DEBUG_ASSERT_NE(max_ind_found, INT32_MAX);
                end_of_search_index = max_ind_found;
            } else {
                end_of_search_index = end;
            }
        } else {
            end_of_search_index = end;
        }
        return std::make_pair(found, end_of_search_index);
    }

    _MultiMatchSelector is_bsearch_left_or_right_most(const BtreeKeyRange& range) const {
        auto selection = range.selection_option();
        if (range.is_simple_search()) { return (_MultiMatchSelector::DO_NOT_CARE); }
        if (selection == _MultiMatchSelector::LEFT_MOST) {
            return (_MultiMatchSelector::LEFT_MOST);
        } else if (selection == _MultiMatchSelector::RIGHT_MOST) {
            return (_MultiMatchSelector::RIGHT_MOST);
        } else if (selection == _MultiMatchSelector::BEST_FIT_TO_CLOSEST_FOR_REMOVE) {
            return (_MultiMatchSelector::LEFT_MOST);
        }
        return (_MultiMatchSelector::DO_NOT_CARE);
    }
};
} // namespace btree
} // namespace sisl
