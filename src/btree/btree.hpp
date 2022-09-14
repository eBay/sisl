/*
 *  Created on: 14-May-2016
 *      Author: Hari Kadayam
 *
 *  Copyright � 2016 Kadayam, Hari. All rights reserved.
 */
#pragma once

#include <atomic>
#include <array>

#include <boost/intrusive_ptr.hpp>
#include "btree_internal.hpp"
#include "btree_req.hpp"
#include "btree_kv.hpp"
#include "btree_node.hpp"

namespace sisl {
namespace btree {

#ifdef INCASE_WE_NEED_COMMON
template < typename K, typename V >
class BtreeCommon {
public:
    void deref_node(BtreeNode< K >* node) = 0;
};
#endif

template < typename K >
using BtreeNodePtr = boost::intrusive_ptr< sisl::btree::BtreeNode< K > >;

template < typename K, typename V >
struct BtreeThreadVariables {
    std::vector< btree_locked_node_info< K, V > > wr_locked_nodes;
    std::vector< btree_locked_node_info< K, V > > rd_locked_nodes;
    BtreeNodePtr< K > force_split_node{nullptr};
};

template < typename K, typename V >
class Btree {
private:
    mutable folly::SharedMutexWritePriority m_btree_lock;
    bnodeid_t m_root_node_id{empty_bnodeid};
    uint32_t m_max_nodes;

    BtreeMetrics m_metrics;
    std::atomic< bool > m_destroyed{false};
    std::atomic< uint64_t > m_total_nodes{0};
    uint32_t m_node_size{4096};
#ifndef NDEBUG
    std::atomic< uint64_t > m_req_id{0};
#endif

    // This workaround of BtreeThreadVariables is needed instead of directly declaring statics
    // to overcome the gcc bug, pointer here: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66944
    static BtreeThreadVariables< K, V >* bt_thread_vars() {
        static thread_local BtreeThreadVariables< K, V >* s_ptr{nullptr};
        if (s_ptr == nullptr) {
            static thread_local BtreeThreadVariables< K, V > inst;
            s_ptr = &inst;
        }
        return s_ptr;
    }

protected:
    BtreeConfig m_bt_cfg;

public:
    /////////////////////////////////////// All External APIs /////////////////////////////
    Btree(const BtreeConfig& cfg);
    virtual ~Btree();
    virtual btree_status_t init(void* op_context);
    btree_status_t put(BtreeMutateRequest& put_req);
    btree_status_t get(BtreeGetRequest& greq) const;
    btree_status_t remove(BtreeRemoveRequest& rreq);
    btree_status_t query(BtreeQueryRequest& query_req, std::vector< std::pair< K, V > >& out_values) const;
    // bool verify_tree(bool update_debug_bm) const;
    virtual std::pair< btree_status_t, uint64_t > destroy_btree(void* context);
    nlohmann::json get_status(int log_level) const;
    void print_tree() const;
    nlohmann::json get_metrics_in_json(bool updated = true);

    // static void set_io_flip();
    // static void set_error_flip();

    // static std::array< std::shared_ptr< BtreeCommon< K, V > >, sizeof(btree_stores_t) > s_btree_stores;
    // static std::mutex s_store_reg_mtx;

protected:
    /////////////////////////// Methods the underlying store is expected to handle ///////////////////////////
    virtual BtreeNodePtr< K > alloc_node(bool is_leaf, bool& is_new_allocation,
                                         const BtreeNodePtr< K >& copy_from = nullptr) = 0;
    virtual BtreeNode< K >* init_node(uint8_t* node_buf, bnodeid_t id, bool init_buf, bool is_leaf);
    virtual btree_status_t read_node(bnodeid_t id, BtreeNodePtr< K >& bnode) const = 0;
    virtual btree_status_t write_node(const BtreeNodePtr< K >& bn, const BtreeNodePtr< K >& dependent_bn,
                                      void* context);
    virtual btree_status_t write_node_sync(const BtreeNodePtr< K >& node, void* context);
    virtual void swap_node(const BtreeNodePtr< K >& node1, const BtreeNodePtr< K >& node2, void* context) = 0;
    virtual btree_status_t refresh_node(const BtreeNodePtr< K >& bn, bool is_write_modifiable, void* context) const = 0;
    virtual void free_node(const BtreeNodePtr< K >& node, void* context) = 0;

    virtual void create_tree_precommit(const BtreeNodePtr< K >& root_node, void* op_context) = 0;
    virtual void split_node_precommit(const BtreeNodePtr< K >& parent_node, const BtreeNodePtr< K >& child_node1,
                                      const BtreeNodePtr< K >& child_node2, bool root_split, bool edge_split,
                                      void* op_context) = 0;
    virtual void merge_node_precommit(bool is_root_merge, const BtreeNodePtr< K >& parent_node,
                                      uint32_t parent_merge_start_idx, const BtreeNodePtr< K >& child_node1,
                                      const std::vector< BtreeNodePtr< K > >* old_child_nodes,
                                      const std::vector< BtreeNodePtr< K > >* replace_child_nodes,
                                      void* op_context) = 0;
    virtual std::string btree_store_type() const = 0;

    /////////////////////////// Methods the application use case is expected to handle ///////////////////////////
    virtual int64_t compute_single_put_needed_size(const V& current_val, const V& new_val) const;
    virtual int64_t compute_range_put_needed_size(const std::vector< std::pair< K, V > >& existing_kvs,
                                                  const V& new_val) const;
    virtual btree_status_t custom_kv_select_for_write(uint8_t node_version,
                                                      const std::vector< std::pair< K, V > >& match_kv,
                                                      std::vector< std::pair< K, V > >& replace_kv,
                                                      const BtreeKeyRange& range,
                                                      const BtreeRangeUpdateRequest& rureq) const;
    virtual btree_status_t custom_kv_select_for_read(uint8_t node_version,
                                                     const std::vector< std::pair< K, V > >& match_kv,
                                                     std::vector< std::pair< K, V > >& replace_kv,
                                                     const BtreeKeyRange& range, const BtreeRangeRequest& qreq) const;

protected:
    /////////////////////////////// Internal Node Management Methods ////////////////////////////////////
    std::pair< btree_status_t, bnodeid_t > create_root_node(void* op_context);
    btree_status_t read_and_lock_root(bnodeid_t id, BtreeNodePtr< K >& node_ptr, locktype_t int_lock_type,
                                      locktype_t leaf_lock_type, void* context) const;
    btree_status_t read_and_lock_child(bnodeid_t child_id, BtreeNodePtr< K >& child_node,
                                       const BtreeNodePtr< K >& parent_node, uint32_t parent_ind,
                                       locktype_t int_lock_type, locktype_t leaf_lock_type, void* context) const;
    btree_status_t read_and_lock_sibling(bnodeid_t id, BtreeNodePtr< K >& node_ptr, locktype_t int_lock_type,
                                         locktype_t leaf_lock_type, void* context) const;
    btree_status_t read_and_lock_node(bnodeid_t id, BtreeNodePtr< K >& node_ptr, locktype_t int_lock_type,
                                      locktype_t leaf_lock_type, void* context) const;
    btree_status_t get_child_and_lock_node(const BtreeNodePtr< K >& node, uint32_t index, BtreeNodeInfo& child_info,
                                           BtreeNodePtr< K >& child_node, locktype_t int_lock_type,
                                           locktype_t leaf_lock_type, void* context) const;
    virtual btree_status_t write_node(const BtreeNodePtr< K >& node, void* context);
    void read_node_or_fail(bnodeid_t id, BtreeNodePtr< K >& node) const;
    btree_status_t upgrade_node(const BtreeNodePtr< K >& my_node, BtreeNodePtr< K > child_node, void* context,
                                locktype_t& cur_lock, locktype_t& child_cur_lock);
    btree_status_t _lock_and_refresh_node(const BtreeNodePtr< K >& node, locktype_t type, void* context,
                                          const char* fname, int line) const;
    btree_status_t _lock_node_upgrade(const BtreeNodePtr< K >& node, void* context, const char* fname, int line);
    void unlock_node(const BtreeNodePtr< K >& node, locktype_t type) const;
    BtreeNodePtr< K > alloc_leaf_node();
    BtreeNodePtr< K > alloc_interior_node();
    void do_free_node(const BtreeNodePtr< K >& node);
    std::pair< btree_status_t, uint64_t > do_destroy();
    void observe_lock_time(const BtreeNodePtr< K >& node, locktype_t type, uint64_t time_spent) const;

    static void _start_of_lock(const BtreeNodePtr< K >& node, locktype_t ltype, const char* fname, int line);
    static bool remove_locked_node(const BtreeNodePtr< K >& node, locktype_t ltype,
                                   btree_locked_node_info< K, V >* out_info);
    static uint64_t end_of_lock(const BtreeNodePtr< K >& node, locktype_t ltype);
#ifndef NDEBUG
    static void check_lock_debug();
#endif

    /////////////////////////////////// Helper Methods ///////////////////////////////////////
    btree_status_t post_order_traversal(locktype_t acq_lock, const auto& cb);
    btree_status_t post_order_traversal(const BtreeNodePtr< K >& node, locktype_t acq_lock, const auto& cb);
    void get_all_kvs(std::vector< pair< K, V > >& kvs) const;
    btree_status_t do_destroy(uint64_t& n_freed_nodes, void* context);
    uint64_t get_btree_node_cnt() const;
    uint64_t get_child_node_cnt(bnodeid_t bnodeid) const;
    void to_string(bnodeid_t bnodeid, std::string& buf) const;
    void validate_sanity_child(const BtreeNodePtr< K >& parent_node, uint32_t ind) const;
    void validate_sanity_next_child(const BtreeNodePtr< K >& parent_node, uint32_t ind) const;
    void print_node(const bnodeid_t& bnodeid) const;

    //////////////////////////////// Impl Methods //////////////////////////////////////////

    ///////// Mutate Impl Methods
    btree_status_t do_put(const BtreeNodePtr< K >& my_node, locktype_t curlock, BtreeMutateRequest& put_req,
                          int ind_hint);
    btree_status_t mutate_write_leaf_node(const BtreeNodePtr< K >& my_node, BtreeMutateRequest& req);
    btree_status_t check_and_split_node(const BtreeNodePtr< K >& my_node, BtreeMutateRequest& req, int ind_hint,
                                        const BtreeNodePtr< K >& child_node, locktype_t& curlock,
                                        locktype_t& child_curlock, int child_ind, bool& split_occured);
    btree_status_t check_split_root(BtreeMutateRequest& req);
    btree_status_t split_node(const BtreeNodePtr< K >& parent_node, const BtreeNodePtr< K >& child_node,
                              uint32_t parent_ind, BtreeKey* out_split_key, bool root_split, void* context);
    bool is_split_needed(const BtreeNodePtr< K >& node, const BtreeConfig& cfg, BtreeMutateRequest& req) const;
    btree_status_t get_start_and_end_ind(const BtreeNodePtr< K >& node, BtreeMutateRequest& req, int& start_ind,
                                         int& end_ind);

    ///////// Remove Impl Methods
    btree_status_t do_remove(const BtreeNodePtr< K >& my_node, locktype_t curlock, BtreeRemoveRequest& rreq);
    btree_status_t check_collapse_root(void* context);
    btree_status_t merge_nodes(const BtreeNodePtr< K >& parent_node, uint32_t start_indx, uint32_t end_indx,
                               void* context);

    ///////// Query Impl Methods
    btree_status_t do_sweep_query(BtreeNodePtr< K >& my_node, BtreeQueryRequest& qreq,
                                  std::vector< std::pair< K, V > >& out_values) const;
    btree_status_t do_traversal_query(const BtreeNodePtr< K >& my_node, BtreeQueryRequest& qreq,
                                      std::vector< std::pair< K, V > >& out_values) const;
#ifdef SERIALIZABLE_QUERY_IMPLEMENTATION
    btree_status_t do_serialzable_query(const BtreeNodePtr< K >& my_node, BtreeSerializableQueryRequest& qreq,
                                        std::vector< std::pair< K, V > >& out_values);
    btree_status_t sweep_query(BtreeQueryRequest& qreq, std::vector< std::pair< K, V > >& out_values);
    btree_status_t serializable_query(BtreeSerializableQueryRequest& qreq,
                                      std::vector< std::pair< K, V > >& out_values);
#endif

    ///////// Get Impl Methods
    btree_status_t do_get(const BtreeNodePtr< K >& my_node, BtreeGetRequest& greq) const;
};
} // namespace btree
} // namespace sisl
