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

#include "logging/logging.h"
#include "btree_node.hpp"
#include "btree_kv.hpp"

SISL_LOGGING_DECL(btree_generics)

namespace sisl {
namespace btree {
#pragma pack(1)
struct btree_obj_record {
    uint16_t m_obj_offset : 14;
    uint16_t reserved : 2;
};

struct var_node_header {
    uint16_t m_tail_arena_offset; // Tail side of the arena where new keys are inserted
    uint16_t m_available_space;
    uint16_t m_init_available_space; // remember initial node area size to later use for compaction
    // TODO:
    // We really dont require storing m_init_available_space in each node.
    // Instead add method in variant node to fetch config

    uint16_t tail_offset() const { return m_tail_arena_offset; }
    uint16_t available_space() const { return m_available_space; }
};
#pragma pack()

/**
 * Internal format of variable node:
 * [var node header][Record][Record].. ...  ... [key][value][key][value]
 *  key and value both can be variying.
 */
template < typename K, typename V >
class VariableNode : public BtreeNode< K, V > {
public:
    VariableNode(uint8_t* node_buf, bnodeid_t id, bool init, bool is_leaf, const BtreeConfig& cfg) :
            BtreeNode< K, V >(node_buf, id, init, is_leaf) {
        if (init) {
            // Tail arena points to the edge of the node as data arena grows backwards. Entire space is now available
            // except for the header itself
            get_var_node_header()->m_init_available_space = BtreeNode< K, V >::node_area_size(cfg);
            get_var_node_header()->m_tail_arena_offset = BtreeNode< K, V >::node_area_size(cfg);
            get_var_node_header()->m_available_space =
                get_var_node_header()->m_tail_arena_offset - sizeof(var_node_header);
        }
    }

    virtual ~VariableNode() = default;

    /* Insert the key and value in provided index
     * Assumption: Node lock is already taken */
    btree_status_t insert(uint32_t ind, const BtreeKey& key, const BtreeValue& val) override {
        LOGTRACEMOD(btree_generics, "{}:{}", key.to_string(), val.to_string());
        auto sz = insert(ind, key.serialize(), val.serialize());
#ifndef NDEBUG
        validate_sanity();
#endif
        if (sz == 0) { return btree_status_t::insert_failed; }
        return btree_status_t::success;
    }

#ifndef NDEBUG
    void validate_sanity() {
        uint32_t i{0};
        // validate if keys are in ascending order
        K prevKey;
        while (i < this->get_total_entries()) {
            K key = get_nth_key(i, false);
            uint64_t kp = *(uint64_t*)key.serialize().bytes;
            if (i > 0 && prevKey.compare(key) > 0) {
                DEBUG_ASSERT(false, "Found non sorted entry: {} -> {}", kp, to_string());
            }
            prevKey = key;
            ++i;
        }
    }
#endif

    /* Update a value in a given index to the provided value. It will support change in size of the new value.
     * Assumption: Node lock is already taken, size check for the node to support new value is already done */
    void update(uint32_t ind, const BtreeValue& val) override {
        // If we are updating the edge value, none of the other logic matter. Just update edge value and move on
        if (ind == this->get_total_entries()) {
            DEBUG_ASSERT_EQ(this->is_leaf(), false);
            this->set_edge_value(val);
            this->inc_gen();
        } else {
            K key = get_nth_key(ind, true);
            update(ind, key, val);
        }
    }

    // TODO - currently we do not support variable size key
    void update(uint32_t ind, const BtreeKey& key, const BtreeValue& val) override {
        LOGTRACEMOD(btree_generics, "Update called:{}", to_string());
        DEBUG_ASSERT_LE(ind, this->get_total_entries());

        // If we are updating the edge value, none of the other logic matter. Just update edge value and move on
        if (ind == this->get_total_entries()) {
            DEBUG_ASSERT_EQ(this->is_leaf(), false);
            this->set_edge_value(val);
            this->inc_gen();
            return;
        }

        // Determine if we are doing same size update or smaller size update, in that case, reuse the space.
        uint16_t nth_key_len = get_nth_key_len(ind);
        uint16_t new_obj_size = nth_key_len + val.serialized_size();
        uint16_t cur_obj_size = get_nth_obj_size(ind);

        if (cur_obj_size >= new_obj_size) {
            uint8_t* val_ptr = (uint8_t*)get_nth_obj(ind) + nth_key_len;
            sisl::blob vblob = val.serialize();
            DEBUG_ASSERT_EQ(vblob.size, val.serialized_size(),
                            "Serialized size returned different after serialization");

            // we can avoid memcpy if addresses of val_ptr and vblob.bytes is same. In place update
            if (val_ptr != vblob.bytes) {
                // TODO - we can reclaim space if new obj size is lower than cur obj size
                // Same or smaller size update, just copy the value blob
                LOGTRACEMOD(btree_generics, "Not an in-place update, have to copying data of size {}", vblob.size);
                memcpy(val_ptr, vblob.bytes, vblob.size);
            } else {
                // do nothing
                LOGTRACEMOD(btree_generics, "In place update, not copying data.");
            }
            set_nth_value_len(get_nth_record_mutable(ind), vblob.size);
            get_var_node_header()->m_available_space += cur_obj_size - new_obj_size;
            this->inc_gen();
            return;
        }

        remove(ind, ind);
        insert(ind, key, val);
        LOGTRACEMOD(btree_generics, "Size changed for either key or value. Had to delete and insert :{}", to_string());
    }

    // ind_s and ind_e are inclusive
    void remove(uint32_t ind_s, uint32_t ind_e) override {
        uint32_t total_entries = this->get_total_entries();
        assert(total_entries >= ind_s);
        assert(total_entries >= ind_e);
        uint32_t recSize = get_record_size();
        uint32_t no_of_elem = ind_e - ind_s + 1;
        if (ind_e == this->get_total_entries()) {
            assert(!this->is_leaf() && this->has_valid_edge());

            V last_1_val = get_nth_value(ind_s - 1, false);
            this->set_edge_value(last_1_val);

            for (uint32_t i = ind_s; i < total_entries; i++) {
                get_var_node_header()->m_available_space += get_nth_key_len(i) + get_nth_value_len(i) + recSize;
            }
            this->sub_entries(total_entries - ind_s + 1);
        } else {
            // claim available memory
            for (uint32_t i = ind_s; i <= ind_e; i++) {
                get_var_node_header()->m_available_space += get_nth_key_len(i) + get_nth_value_len(i) + recSize;
            }
            uint8_t* rec_ptr = get_nth_record_mutable(ind_s);
            memmove(rec_ptr, rec_ptr + recSize * no_of_elem, (this->get_total_entries() - ind_e - 1) * recSize);

            this->sub_entries(no_of_elem);
        }
        this->inc_gen();
    }

    V get(uint32_t ind, bool copy) const override {
        // Need edge index
        if (ind == this->get_total_entries()) {
            assert(!this->is_leaf());
            assert(this->has_valid_edge());
            return this->get_edge_value();
        } else {
            return get_nth_value(ind, copy);
        }
    }

    uint32_t move_out_to_right_by_entries(const BtreeConfig& cfg, BtreeNode< K, V >& o, uint32_t nentries) override {
        auto& other = static_cast< VariableNode& >(o);
        const auto this_gen = this->get_gen();
        const auto other_gen = other.get_gen();

        const auto this_nentries = this->get_total_entries();
        nentries = std::min(nentries, this_nentries);
        if (nentries == 0) { return 0; /* Nothing to move */ }

        const uint32_t start_ind = this_nentries - 1;
        const uint32_t end_ind = this_nentries - nentries;
        uint32_t ind = start_ind;
        bool full_move{false};
        while (ind >= end_ind) {
            // Get the ith key and value blob and then remove the entry from here and insert to the other node
            sisl::blob kb;
            kb.bytes = (uint8_t*)get_nth_obj(ind);
            kb.size = get_nth_key_len(ind);

            sisl::blob vb;
            vb.bytes = kb.bytes + kb.size;
            vb.size = get_nth_value_len(ind);

            auto sz = other.insert(0, kb, vb);
            if (!sz) { break; }
            if (ind == 0) {
                full_move = true;
                break;
            }
            --ind;
        }

        if (!this->is_leaf() && (other.get_total_entries() != 0)) {
            // Incase this node is an edge node, move the stick to the right hand side node
            other.set_edge_id(this->get_edge_id());
            this->invalidate_edge();
        }
        remove(full_move ? 0u : ind + 1, start_ind); // Remove all entries in bulk

        // Remove and insert would have set the gen multiple increments, just reset it to increment only by 1
        // TODO: This is bit ugly but needed in-order to avoid repeat the same code again, but see if we can produce
        // interface around it.
        this->set_gen(this_gen + 1);
        other.set_gen(other_gen + 1);

        return (start_ind - ind);
    }

    uint32_t move_out_to_right_by_size(const BtreeConfig& cfg, BtreeNode< K, V >& o, uint32_t size_to_move) override {
        auto& other = static_cast< VariableNode& >(o);
        uint32_t moved_size = 0U;
        auto this_gen = this->get_gen();
        auto other_gen = other.get_gen();

        uint32_t ind = this->get_total_entries() - 1;
        while (ind > 0) {
            sisl::blob kb;
            kb.bytes = (uint8_t*)get_nth_obj(ind);
            kb.size = get_nth_key_len(ind);

            sisl::blob vb;
            vb.bytes = kb.bytes + kb.size;
            vb.size = get_nth_value_len(ind);

            auto sz = other.insert(0, kb, vb); // Keep on inserting on the first index, thus moving everything to right
            if (!sz) break;
            moved_size += sz;
            --ind;
            if ((kb.size + vb.size + get_record_size()) > size_to_move) {
                // We reached threshold of how much we could move
                break;
            }
            size_to_move -= sz;
        }
        remove(ind + 1, this->get_total_entries() - 1);

        if (!this->is_leaf() && (other.get_total_entries() != 0)) {
            // Incase this node is an edge node, move the stick to the right hand side node
            other.set_edge_id(this->get_edge_id());
            this->invalidate_edge();
        }

        // Remove and insert would have set the gen multiple increments, just reset it to increment only by 1
        // TODO: This is bit ugly but needed in-order to avoid repeat the same code again, but see if we can produce
        // interface around it.
        this->set_gen(this_gen + 1);
        other.set_gen(other_gen + 1);

        return moved_size;
    }

    uint32_t move_in_from_right_by_entries(const BtreeConfig& cfg, BtreeNode< K, V >& o, uint32_t nentries) override {
        auto& other = static_cast< VariableNode& >(o);
        auto this_gen = this->get_gen();
        auto other_gen = other.get_gen();
        nentries = std::min(nentries, other.get_total_entries());

        if (nentries == 0) { return 0; /* Nothing to move */ }
        uint32_t other_ind = 0;
        while (nentries) {
            // Get the ith key and value blob and then remove the entry from here and insert to the other node
            sisl::blob kb;
            kb.bytes = (uint8_t*)other.get_nth_obj(other_ind);
            kb.size = other.get_nth_key_len(other_ind);

            sisl::blob vb;
            vb.bytes = kb.bytes + kb.size;
            vb.size = other.get_nth_value_len(other_ind);

            auto sz = insert(this->get_total_entries(), kb, vb);
            if (!sz) { break; }
            --nentries;
            ++other_ind;
        }

        other.remove(0, other_ind - 1); // Remove all entries in bulk
        assert(other.get_total_entries() == nentries);

        if (!other.is_leaf() && (other.get_total_entries() == 0)) {
            // Incase other node is an edge node and we moved all the data into this node, move over the edge info as
            // well.
            this->set_edge_id(other.get_edge_id());
            other.invalidate_edge();
        }

        // Remove and insert would have set the gen multiple increments, just reset it to increment only by 1
        // TODO: This is bit ugly but needed in-order to avoid repeat the same code again, but see if we can produce
        // interface around it.
        this->set_gen(this_gen + 1);
        other.set_gen(other_gen + 1);

        return (other_ind);
    }

    uint32_t move_in_from_right_by_size(const BtreeConfig& cfg, BtreeNode< K, V >& o, uint32_t size_to_move) override {
        auto& other = static_cast< VariableNode& >(o);
        uint32_t moved_size = 0U;
        auto this_gen = this->get_gen();
        auto other_gen = other.get_gen();

        uint32_t ind = 0;
        while (ind < this->get_total_entries()) {
            sisl::blob kb;
            kb.bytes = (uint8_t*)other.get_nth_obj(ind);
            kb.size = other.get_nth_key_len(ind);

            sisl::blob vb;
            vb.bytes = kb.bytes + kb.size;
            vb.size = other.get_nth_value_len(ind);

            if ((kb.size + vb.size + other.get_record_size()) > size_to_move) {
                // We reached threshold of how much we could move
                break;
            }
            auto sz = insert(this->get_total_entries(), kb, vb); // Keep on inserting on the last index.
            if (!sz) break;
            moved_size += sz;
            ind++;
            size_to_move -= sz;
        }
        if (ind) other.remove(0, ind - 1);

        if (!other.is_leaf() && (other.get_total_entries() == 0)) {
            // Incase other node is an edge node and we moved all the data into this node, move over the edge info as
            // well.
            this->set_edge_id(other.get_edge_id());
            other.invalidate_edge();
        }

        // Remove and insert would have set the gen multiple increments, just reset it to increment only by 1
        // TODO: This is bit ugly but needed in-order to avoid repeat the same code again, but see if we can produce
        // interface around it.
        this->set_gen(this_gen + 1);
        other.set_gen(other_gen + 1);

        return moved_size;
    }
    void append(uint32_t ind, const BtreeKey& key, const BtreeValue& val) override {
        RELEASE_ASSERT(false, "Append operation is not supported on var node");
    }

    uint32_t get_available_size(const BtreeConfig& cfg) const override {
        return get_var_node_header_const()->m_available_space;
    }

    uint32_t get_nth_obj_size(uint32_t ind) const override { return get_nth_key_len(ind) + get_nth_value_len(ind); }

    void set_nth_key(uint32_t ind, const BtreeKey& key) {
        const auto kb = key.serialize();
        assert(ind < this->get_total_entries());
        assert(kb.size == get_nth_key_len(ind));
        memcpy(uintptr_cast(get_nth_obj(ind)), kb.bytes, kb.size);
    }

    virtual uint16_t get_nth_key_len(uint32_t ind) const = 0;
    virtual uint16_t get_nth_value_len(uint32_t ind) const = 0;
    virtual uint16_t get_record_size() const = 0;
    virtual void set_nth_key_len(uint8_t* rec_ptr, uint16_t key_len) = 0;
    virtual void set_nth_value_len(uint8_t* rec_ptr, uint16_t value_len) = 0;

    K get_nth_key(uint32_t ind, bool copy) const {
        assert(ind < this->get_total_entries());
        sisl::blob b{const_cast< uint8_t* >(get_nth_obj(ind)), get_nth_key_len(ind)};
        return K{b, copy};
    }

    V get_nth_value(uint32_t ind, bool copy) const {
        assert(ind < this->get_total_entries());
        sisl::blob b{const_cast< uint8_t* >(get_nth_obj(ind)) + get_nth_key_len(ind), get_nth_value_len(ind)};
        return V{b, copy};
    }

    std::string to_string(bool print_friendly = false) const override {
        auto str = fmt::format(
            "{}id={} nEntries={} {} free_space={} ",
            (print_friendly ? "---------------------------------------------------------------------\n" : ""),
            this->get_node_id(), this->get_total_entries(), (this->is_leaf() ? "LEAF" : "INTERIOR"),
            get_var_node_header_const()->m_available_space);
        if (!this->is_leaf() && (this->has_valid_edge())) {
            fmt::format_to(std::back_inserter(str), "edge_id={} ", this->get_edge_id());
        }
        for (uint32_t i{0}; i < this->get_total_entries(); ++i) {
            fmt::format_to(std::back_inserter(str), "{}Entry{} [Key={} Val={}]", (print_friendly ? "\n\t" : " "), i + 1,
                           get_nth_key(i, false).to_string(), get(i, false).to_string());
        }
        return str;
    }

    int compare_nth_key(const BtreeKey& cmp_key, uint32_t ind) const {
        return get_nth_key(ind, false).compare(cmp_key);
    }

    int compare_nth_key_range(const BtreeKeyRange& range, uint32_t ind) const {
        return get_nth_key(ind, false).compare_range(range);
    }

protected:
    uint32_t insert(uint32_t ind, const sisl::blob& key_blob, const sisl::blob& val_blob) {
        assert(ind <= this->get_total_entries());
        LOGTRACEMOD(btree_generics, "{}:{}:{}:{}", ind, get_var_node_header()->tail_offset(), get_arena_free_space(),
                    get_var_node_header()->available_space());
        uint16_t obj_size = key_blob.size + val_blob.size;
        uint16_t to_insert_size = obj_size + get_record_size();
        if (to_insert_size > get_var_node_header()->available_space()) {
            LOGDEBUG("insert failed insert size {} available size {}", to_insert_size,
                     get_var_node_header()->available_space());
            return 0;
        }

        // If we don't have enough space in the tail arena area, we need to compact and get the space.
        if (to_insert_size > get_arena_free_space()) {
            compact();
            assert(to_insert_size <=
                   get_arena_free_space()); // Expect after compaction to have available space to insert
        }

        // Create a room for a new record
        uint8_t* rec_ptr = uintptr_cast(get_nth_record_mutable(ind));
        memmove((void*)(rec_ptr + get_record_size()), rec_ptr, (this->get_total_entries() - ind) * get_record_size());

        // Move up the tail area
        assert(get_var_node_header()->m_tail_arena_offset > obj_size);
        get_var_node_header()->m_tail_arena_offset -= obj_size;
        get_var_node_header()->m_available_space -= (obj_size + get_record_size());

        // Create a new record
        set_nth_key_len(rec_ptr, key_blob.size);
        set_nth_value_len(rec_ptr, val_blob.size);
        set_record_data_offset(rec_ptr, get_var_node_header()->m_tail_arena_offset);

        // Copy the contents of key and value in the offset
        uint8_t* raw_data_ptr = offset_to_ptr_mutable(get_var_node_header()->m_tail_arena_offset);
        memcpy(raw_data_ptr, key_blob.bytes, key_blob.size);
        raw_data_ptr += key_blob.size;
        memcpy(raw_data_ptr, val_blob.bytes, val_blob.size);

        // Increment the entries and generation number
        this->inc_entries();
        this->inc_gen();

#ifndef NDEBUG
        this->validate_sanity();
#endif

#ifdef DEBUG
        // print();
#endif
        return to_insert_size;
    }

    /*
     * This method compacts and provides contiguous tail arena space
     * so that available space == tail arena space
     * */
    void compact() {
#ifndef NDEBUG
        this->validate_sanity();
#endif
        // temp ds to sort records in stack space
        struct Record {
            uint16_t m_obj_offset;
            uint16_t orig_record_index;
        };

        uint32_t no_of_entries = this->get_total_entries();
        if (no_of_entries == 0) {
            // this happens when  there is only entry and in update, we first remove and than insert
            get_var_node_header()->m_tail_arena_offset = get_var_node_header()->m_init_available_space;
            LOGTRACEMOD(btree_generics, "Full available size reclaimed");
            return;
        }
        std::vector< Record > rec;
        rec.reserve(no_of_entries);

        uint32_t ind = 0;
        while (ind < no_of_entries) {
            btree_obj_record* rec_ptr = (btree_obj_record*)(get_nth_record_mutable(ind));
            rec[ind].m_obj_offset = rec_ptr->m_obj_offset;
            rec[ind].orig_record_index = ind;
            ind++;
        }

        // use comparator to sort based on m_obj_offset in desc order
        std::sort(rec.begin(), rec.begin() + no_of_entries,
                  [](Record const& a, Record const& b) -> bool { return b.m_obj_offset < a.m_obj_offset; });

        uint16_t last_offset = get_var_node_header()->m_init_available_space;

        ind = 0;
        uint16_t sparce_space = 0;
        // loop records
        while (ind < no_of_entries) {
            uint16_t total_key_value_len =
                get_nth_key_len(rec[ind].orig_record_index) + get_nth_value_len(rec[ind].orig_record_index);
            sparce_space = last_offset - (rec[ind].m_obj_offset + total_key_value_len);
            if (sparce_space > 0) {
                // do compaction
                uint8_t* old_key_ptr = (uint8_t*)get_nth_obj(rec[ind].orig_record_index);
                uint8_t* raw_data_ptr = old_key_ptr + sparce_space;
                memmove(raw_data_ptr, old_key_ptr, total_key_value_len);

                // update original record
                btree_obj_record* rec_ptr = (btree_obj_record*)(get_nth_record_mutable(rec[ind].orig_record_index));
                rec_ptr->m_obj_offset += sparce_space;

                last_offset = rec_ptr->m_obj_offset;

            } else {
                assert(sparce_space == 0);
                last_offset = rec[ind].m_obj_offset;
            }
            ind++;
        }
        get_var_node_header()->m_tail_arena_offset = last_offset;
#ifndef NDEBUG
        this->validate_sanity();
#endif
        LOGTRACEMOD(btree_generics, "Sparse space reclaimed:{}", sparce_space);
    }

    const uint8_t* get_nth_record(uint32_t ind) const {
        return this->node_data_area_const() + sizeof(var_node_header) + (ind * get_record_size());
    }
    uint8_t* get_nth_record_mutable(uint32_t ind) {
        return this->node_data_area() + sizeof(var_node_header) + (ind * get_record_size());
    }

    const uint8_t* get_nth_obj(uint32_t ind) const {
        return offset_to_ptr(((btree_obj_record*)get_nth_record(ind))->m_obj_offset);
    }
    uint8_t* get_nth_obj_mutable(uint32_t ind) {
        return offset_to_ptr_mutable(((btree_obj_record*)get_nth_record(ind))->m_obj_offset);
    }

    void set_record_data_offset(uint8_t* rec_ptr, uint16_t offset) {
        auto r = (btree_obj_record*)rec_ptr;
        r->m_obj_offset = offset;
    }

    uint8_t* offset_to_ptr_mutable(uint16_t offset) { return this->node_data_area() + offset; }

    const uint8_t* offset_to_ptr(uint16_t offset) const { return this->node_data_area_const() + offset; }

    ///////////// Other Private Methods //////////////////
    inline var_node_header* get_var_node_header() { return r_cast< var_node_header* >(this->node_data_area()); }

    inline const var_node_header* get_var_node_header_const() const {
        return r_cast< const var_node_header* >(this->node_data_area_const());
    }

    uint16_t get_arena_free_space() const {
        return get_var_node_header_const()->m_tail_arena_offset - sizeof(var_node_header) -
            (this->get_total_entries() * get_record_size());
    }
};

template < typename K, typename V >
class VarKeySizeNode : public VariableNode< K, V > {
public:
    VarKeySizeNode(uint8_t* node_buf, bnodeid_t id, bool init, bool is_leaf, const BtreeConfig& cfg) :
            VariableNode< K, V >(node_buf, id, init, is_leaf, cfg) {
        this->set_node_type(btree_node_type::VAR_KEY);
    }

    uint16_t get_nth_key_len(uint32_t ind) const override {
        return r_cast< const var_key_record* >(this->get_nth_record(ind))->m_key_len;
    }
    uint16_t get_nth_value_len(uint32_t ind) const override { return V::get_fixed_size(); }
    uint16_t get_record_size() const override { return sizeof(var_key_record); }

    void set_nth_key_len(uint8_t* rec_ptr, uint16_t key_len) override {
        r_cast< var_key_record* >(rec_ptr)->m_key_len = key_len;
    }
    void set_nth_value_len(uint8_t* rec_ptr, uint16_t value_len) override { assert(value_len == V::get_fixed_size()); }

private:
#pragma pack(1)
    struct var_key_record : public btree_obj_record {
        uint16_t m_key_len : 14;
        uint16_t reserved : 2;
    };
#pragma pack()
};

/***************** Template Specialization for variable value records ******************/
template < typename K, typename V >
class VarValueSizeNode : public VariableNode< K, V > {
public:
    VarValueSizeNode(uint8_t* node_buf, bnodeid_t id, bool init, bool is_leaf, const BtreeConfig& cfg) :
            VariableNode< K, V >(node_buf, id, init, is_leaf, cfg) {
        this->set_node_type(btree_node_type::VAR_VALUE);
    }

    uint16_t get_nth_key_len(uint32_t ind) const override { return K::get_fixed_size(); }
    uint16_t get_nth_value_len(uint32_t ind) const override {
        return r_cast< const var_value_record* >(this->get_nth_record(ind))->m_value_len;
    }
    uint16_t get_record_size() const override { return sizeof(var_value_record); }

    void set_nth_key_len(uint8_t* rec_ptr, uint16_t key_len) override { assert(key_len == K::get_fixed_size()); }
    void set_nth_value_len(uint8_t* rec_ptr, uint16_t value_len) override {
        r_cast< var_value_record* >(rec_ptr)->m_value_len = value_len;
    }

private:
#pragma pack(1)
    struct var_value_record : public btree_obj_record {
        uint16_t m_value_len : 14;
        uint16_t reserved : 2;
    };
#pragma pack()
};

/***************** Template Specialization for variable object records ******************/
template < typename K, typename V >
class VarObjSizeNode : public VariableNode< K, V > {
public:
    VarObjSizeNode(uint8_t* node_buf, bnodeid_t id, bool init, bool is_leaf, const BtreeConfig& cfg) :
            VariableNode< K, V >(node_buf, id, init, is_leaf, cfg) {
        this->set_node_type(btree_node_type::VAR_OBJECT);
    }

    uint16_t get_nth_key_len(uint32_t ind) const override {
        return r_cast< const var_obj_record* >(this->get_nth_record(ind))->m_key_len;
    }
    uint16_t get_nth_value_len(uint32_t ind) const override {
        return r_cast< const var_obj_record* >(this->get_nth_record(ind))->m_value_len;
    }
    uint16_t get_record_size() const override { return sizeof(var_obj_record); }

    void set_nth_key_len(uint8_t* rec_ptr, uint16_t key_len) override {
        r_cast< var_obj_record* >(rec_ptr)->m_key_len = key_len;
    }
    void set_nth_value_len(uint8_t* rec_ptr, uint16_t value_len) override {
        r_cast< var_obj_record* >(rec_ptr)->m_value_len = value_len;
    }

private:
#pragma pack(1)
    struct var_obj_record : public btree_obj_record {
        uint16_t m_key_len : 14;
        uint16_t reserved : 2;

        uint16_t m_value_len : 14;
        uint16_t reserved2 : 2;
    };
#pragma pack()
};
} // namespace btree
} // namespace sisl
