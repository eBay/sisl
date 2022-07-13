/*
 * simple_node.hpp
 *
 *  Created on: 16-May-2016
 *      Author: Hari Kadayam
 *
 *  Copyright � 2016 Kadayam, Hari. All rights reserved.
 */
#pragma once

#include "btree_node.hpp"
#include "btree_kv.hpp"
#include "btree_internal.hpp"

using namespace std;
using namespace boost;

namespace sisl {
namespace btree {

template < typename K, typename V >
class SimpleNode : public BtreeNode< K, V > {
public:
    SimpleNode(uint8_t* node_buf, bnodeid_t id, bool init, bool is_leaf) :
            BtreeNode< K, V >(node_buf, id, init, is_leaf) {
        this->set_node_type(btree_node_type::SIMPLE);
    }

    // Insert the key and value in provided index
    // Assumption: Node lock is already taken
    void insert(uint32_t ind, const BtreeKey& key, const BtreeValue& val) override {
        uint32_t sz = (this->get_total_entries() - (ind + 1) + 1) * get_nth_obj_size(0);

        if (sz != 0) { std::memmove(get_nth_obj(ind + 1), get_nth_obj(ind), sz); }
        this->set_nth_obj(ind, key, val);
        this->inc_entries();
        this->inc_gen();

#ifndef NDEBUG
        validate_sanity();
#endif
    }

    V get(uint32_t ind, bool copy) const override { return get_nth_value(ind, copy); }

    void update(uint32_t ind, const BtreeValue& val) override {
        set_nth_value(ind, val);

        // TODO: Check if we need to upgrade the gen and impact of doing  so with performance. It is especially
        // needed for non similar key/value pairs
        this->inc_gen();
#ifndef NDEBUG
        validate_sanity();
#endif
    }

    void update(uint32_t ind, const BtreeKey& key, const BtreeValue& val) override {
        set_nth_obj(ind, key, val);
        this->inc_gen();
    }

    // ind_s and ind_e are inclusive
    void remove(uint32_t ind_s, uint32_t ind_e) override {
        uint32_t total_entries = this->get_total_entries();
        DEBUG_ASSERT_GE(total_entries, ind_s, "node={}", to_string());
        DEBUG_ASSERT_GE(total_entries, ind_e, "node={}", to_string());

        if (ind_e == total_entries) { // edge entry
            DEBUG_ASSERT((!this->is_leaf() && this->has_valid_edge()), "node={}", to_string());
            // Set the last key/value as edge entry and by decrementing entry count automatically removed the last
            // entry.
            this->set_nth_value(total_entries, get_nth_value(ind_s - 1, false));
            this->sub_entries(total_entries - ind_s + 1);
        } else {
            uint32_t sz = (total_entries - ind_e - 1) * get_nth_obj_size(0);

            if (sz != 0) { std::memmove(get_nth_obj(ind_s), get_nth_obj(ind_e + 1), sz); }
            this->sub_entries(ind_e - ind_s + 1);
        }
        this->inc_gen();
#ifndef NDEBUG
        validate_sanity();
#endif
    }

    void append(uint32_t ind, const BtreeKey& key, const BtreeValue& val) override {
        RELEASE_ASSERT(false, "Append operation is not supported on simple node");
    }

    uint32_t move_out_to_right_by_entries(const BtreeConfig& cfg, BtreeNode< K, V >& o, uint32_t nentries) override {
        auto& other_node = s_cast< SimpleNode< K, V >& >(o);

        // Minimum of whats to be moved out and how many slots available in other node
        nentries = std::min({nentries, this->get_total_entries(), other_node.get_available_entries(cfg)});
        uint32_t sz = nentries * get_nth_obj_size(0);

        if (sz != 0) {
            uint32_t othersz = other_node.get_total_entries() * other_node.get_nth_obj_size(0);
            std::memmove(other_node.get_nth_obj(nentries), other_node.get_nth_obj(0), othersz);
            std::memmove(other_node.get_nth_obj(0), get_nth_obj(this->get_total_entries() - nentries), sz);
        }

        other_node.add_entries(nentries);
        this->sub_entries(nentries);

        // If there is an edgeEntry in this node, it needs to move to move out as well.
        if (!this->is_leaf() && this->has_valid_edge()) {
            other_node.set_edge_id(this->get_edge_id());
            this->invalidate_edge();
        }

        other_node.inc_gen();
        this->inc_gen();

#ifndef NDEBUG
        validate_sanity();
#endif
        return nentries;
    }

    uint32_t move_out_to_right_by_size(const BtreeConfig& cfg, BtreeNode< K, V >& o, uint32_t size) override {
        return (get_nth_obj_size(0) * move_out_to_right_by_entries(cfg, o, size / get_nth_obj_size(0)));
    }

    uint32_t move_in_from_right_by_entries(const BtreeConfig& cfg, BtreeNode< K, V >& o, uint32_t nentries) override {
        auto& other_node = s_cast< SimpleNode< K, V >& >(o);

        // Minimum of whats to be moved and how many slots available
        nentries = std::min({nentries, other_node.get_total_entries(), get_available_entries(cfg)});
        uint32_t sz = nentries * get_nth_obj_size(0);
        if (sz != 0) {
            uint32_t othersz = (other_node.get_total_entries() - nentries) * other_node.get_nth_obj_size(0);
            std::memmove(get_nth_obj(this->get_total_entries()), other_node.get_nth_obj(0), sz);
            std::memmove(other_node.get_nth_obj(0), other_node.get_nth_obj(nentries), othersz);
        }

        other_node.sub_entries(nentries);
        this->add_entries(nentries);

        // If next node does not have any more entries, but only a edge entry
        // we need to move that to us, so that if need be next node could be freed.
        if ((other_node.get_total_entries() == 0) && other_node.has_valid_edge()) {
            DEBUG_ASSERT_EQ(this->has_valid_edge(), false, "node={}", to_string());
            this->set_edge_id(other_node.get_edge_id());
            other_node.invalidate_edge();
        }

        other_node.inc_gen();
        this->inc_gen();

#ifndef NDEBUG
        validate_sanity();
#endif
        return nentries;
    }

    uint32_t move_in_from_right_by_size(const BtreeConfig& cfg, BtreeNode< K, V >& o, uint32_t size) override {
        return (get_nth_obj_size(0) * move_in_from_right_by_entries(cfg, o, size / get_nth_obj_size(0)));
    }

    uint32_t get_available_size(const BtreeConfig& cfg) const override {
        return (cfg.node_area_size() - (this->get_total_entries() * get_nth_obj_size(0)));
    }

    K get_nth_key(uint32_t ind, bool copy) const override {
        DEBUG_ASSERT_LT(ind, this->get_total_entries(), "node={}", to_string());
        sisl::blob b;
        b.bytes = (uint8_t*)(this->node_data_area_const() + (get_nth_obj_size(ind) * ind));
        b.size = get_obj_key_size(ind);
        return K{b, copy};
    }

    V get_nth_value(uint32_t ind, bool copy) const override {
        DEBUG_ASSERT_LT(ind, this->get_total_entries(), "node={}", to_string());
        sisl::blob b;
        if (ind == this->get_total_entries()) {
            RELEASE_ASSERT_EQ(this->is_leaf(), false, "setting value outside bounds on leaf node");
            DEBUG_ASSERT_EQ(this->has_valid_edge(), true, "node={}", to_string());
            b.bytes = const_cast< uint8_t* >(reinterpret_cast< const uint8_t* >(this->get_edge_id()));
            b.size = sizeof(bnodeid_t);
        } else {
            b.bytes = const_cast< uint8_t* >(reinterpret_cast< const uint8_t* >(
                this->node_data_area_const() + (get_nth_obj_size(ind) * ind) + get_obj_key_size(ind)));
            b.size = V::get_fixed_size();
        }
        return V{b, copy};
    }

    std::string to_string(bool print_friendly = false) const override {
        auto str = fmt::format("{}id={} nEntries={} {} ",
                               (print_friendly ? "------------------------------------------------------------\n" : ""),
                               this->get_node_id(), this->get_total_entries(), (this->is_leaf() ? "LEAF" : "INTERIOR"));
        if (!this->is_leaf() && (this->has_valid_edge())) {
            fmt::format_to(std::back_inserter(str), "edge_id={} ", this->get_edge_id());
        }

        for (uint32_t i{0}; i < this->get_total_entries(); ++i) {
            fmt::format_to(std::back_inserter(str), "{}Entry{} [Key={} Val={}]", (print_friendly ? "\n\t" : " "), i + 1,
                           get_nth_key(i, false).to_string(), get(i, false).to_string());
        }
        return str;
    }

#ifndef NDEBUG
    void validate_sanity() {
        if (this->get_total_entries() == 0) { return; }

        // validate if keys are in ascending order
        uint32_t i{1};
        K prevKey = get_nth_key(0, false);

        while (i < this->get_total_entries()) {
            K key = get_nth_key(i, false);
            if (i > 0 && prevKey.compare(key) > 0) {
                LOGDEBUG("non sorted entry : {} -> {} ", prevKey.to_string(), key.to_string());
                DEBUG_ASSERT(false, "node={}", to_string());
            }
            ++i;
            prevKey = key;
        }
    }
#endif

    inline uint32_t get_nth_obj_size(uint32_t ind) const override {
        return (get_obj_key_size(ind) + get_obj_value_size(ind));
    }

    int compare_nth_key(const BtreeKey& cmp_key, uint32_t ind) const override {
        return get_nth_key(ind, false).compare(cmp_key);
    }

    int compare_nth_key_range(const BtreeKeyRange& range, uint32_t ind) const override {
        return get_nth_key(ind, false).compare_range(range);
    }

    /////////////// Other Internal Methods /////////////
    void set_nth_obj(uint32_t ind, const BtreeKey& k, const BtreeValue& v) {
        if (ind > this->get_total_entries()) {
            set_nth_value(ind, v);
        } else {
            uint8_t* entry = this->node_data_area() + (get_nth_obj_size(ind) * ind);
            sisl::blob key_blob = k.serialize();
            memcpy((void*)entry, key_blob.bytes, key_blob.size);

            sisl::blob val_blob = v.serialize();
            memcpy((void*)(entry + key_blob.size), val_blob.bytes, val_blob.size);
        }
    }

    uint32_t get_available_entries(const BtreeConfig& cfg) const {
        return get_available_size(cfg) / get_nth_obj_size(0);
    }

    inline uint32_t get_obj_key_size(uint32_t ind) const { return K::get_fixed_size(); }

    inline uint32_t get_obj_value_size(uint32_t ind) const { return V::get_fixed_size(); }

    uint8_t* get_nth_obj(uint32_t ind) { return (this->node_data_area() + (get_nth_obj_size(ind) * ind)); }

    void set_nth_key(uint32_t ind, BtreeKey* key) {
        uint8_t* entry = this->node_data_area() + (get_nth_obj_size(ind) * ind);
        sisl::blob b = key->serialize();
        memcpy(entry, b.bytes, b.size);
    }

    void set_nth_value(uint32_t ind, const BtreeValue& v) {
        sisl::blob b = v.serialize();
        if (ind > this->get_total_entries()) {
            RELEASE_ASSERT_EQ(this->is_leaf(), false, "setting value outside bounds on leaf node");
            DEBUG_ASSERT_EQ(b.size, sizeof(bnodeid_t), "Invalid value size being set for non-leaf node");
            this->set_edge_id(*r_cast< bnodeid_t* >(b.bytes));
        } else {
            uint8_t* entry = this->node_data_area() + (get_nth_obj_size(ind) * ind) + get_obj_key_size(ind);
            std::memcpy(entry, b.bytes, b.size);
        }
    }
};
} // namespace btree
} // namespace sisl
