#pragma once
#include "btree.hpp"

namespace sisl {
namespace btree {

/* This function does the heavy lifiting of co-ordinating inserts. It is a recursive function which walks
 * down the tree.
 *
 * NOTE: It expects the node it operates to be locked (either read or write) and also the node should not be
 * full.
 *
 * Input:
 * myNode      = Node it operates on
 * curLock     = Type of lock held for this node
 * put_req           = Key to insert
 * v           = Value to insert
 * ind_hint    = If we already know which slot to insert to, if not -1
 * put_type    = Type of the put (refer to structure btree_put_type)
 * is_end_path = set to true only for last path from root to tree, for range put
 * op          = tracks multi node io.
 */
template < typename K, typename V >
btree_status_t Btree< K, V >::do_put(const BtreeNodePtr< K >& my_node, locktype_t curlock, BtreeMutateRequest& put_req,
                                     int ind_hint) {
    btree_status_t ret = btree_status_t::success;
    int curr_ind = -1;

    if (my_node->is_leaf()) {
        /* update the leaf node */
        BT_NODE_LOG_ASSERT_EQ(curlock, locktype_t::WRITE, my_node);
        ret = mutate_write_leaf_node(my_node, put_req);
        unlock_node(my_node, curlock);
        return ret;
    }

retry:
    int start_ind = 0, end_ind = -1;

    /* Get the start and end ind in a parent node for the range updates. For
     * non range updates, start ind and end ind are same.
     */
    ret = get_start_and_end_ind(my_node, put_req, start_ind, end_ind);
    if (ret != btree_status_t::success) { goto out; }

    BT_NODE_DBG_ASSERT((curlock == locktype_t::READ || curlock == locktype_t::WRITE), my_node, "unexpected locktype {}",
                       curlock);
    curr_ind = start_ind;

    while (curr_ind <= end_ind) { // iterate all matched childrens
#ifdef _PRERELEASE
        if (curr_ind - start_ind > 1 && homestore_flip->test_flip("btree_leaf_node_split")) {
            ret = btree_status_t::retry;
            goto out;
        }
#endif
        locktype_t child_cur_lock = locktype_t::NONE;

        // Get the childPtr for given key.
        BtreeNodeInfo child_info;
        BtreeNodePtr< K > child_node;
        ret = get_child_and_lock_node(my_node, curr_ind, child_info, child_node, locktype_t::READ, locktype_t::WRITE,
                                      put_req_op_ctx(put_req));
        if (ret != btree_status_t::success) {
            if (ret == btree_status_t::not_found) {
                // Either the node was updated or mynode is freed. Just proceed again from top.
                /* XXX: Is this case really possible as we always take the parent lock and never
                 * release it.
                 */
                ret = btree_status_t::retry;
            }
            goto out;
        }

        // Directly get write lock for leaf, since its an insert.
        child_cur_lock = (child_node->is_leaf()) ? locktype_t::WRITE : locktype_t::READ;

        /* Get subrange if it is a range update */
        if (is_range_update_req(put_req) && child_node->is_leaf()) {
            /* We get the subrange only for leaf because this is where we will be inserting keys. In interior
             * nodes, keys are always propogated from the lower nodes.
             */
            BtreeSearchState& search_state = to_range_update_req(put_req).search_state();
            search_state.set_current_sub_range(my_node->get_subrange(search_state.next_range(), curr_ind));

            BT_NODE_LOG(DEBUG, my_node, "Subrange:s:{},e:{},c:{},nid:{},edgeid:{},sk:{},ek:{}", start_ind, end_ind,
                        curr_ind, my_node->get_node_id(), my_node->get_edge_id(),
                        search_state.current_sub_range().start_key().to_string(),
                        search_state.current_sub_range().end_key().to_string());
        }

        /* check if child node is needed to split */
        bool split_occured = false;
        ret = check_and_split_node(my_node, put_req, ind_hint, child_node, curlock, child_cur_lock, curr_ind,
                                   split_occured);
        if (ret != btree_status_t::success) { goto out; }
        if (split_occured) {
            ind_hint = -1; // Since split is needed, hint is no longer valid
            goto retry;
        }

#ifndef NDEBUG
        K ckey, pkey;
        if (curr_ind != int_cast(my_node->get_total_entries())) { // not edge
            pkey = my_node->get_nth_key(curr_ind, true);
            if (child_node->get_total_entries() != 0) {
                ckey = child_node->get_last_key();
                if (!child_node->is_leaf()) {
                    BT_NODE_DBG_ASSERT_EQ(ckey.compare(pkey), 0, my_node);
                } else {
                    BT_NODE_DBG_ASSERT_LE(ckey.compare(pkey), 0, my_node);
                }
            }
            // BT_NODE_DBG_ASSERT_EQ((is_range_update_req(put_req) || k.compare(pkey) <= 0), true, child_node);
        }
        if (curr_ind > 0) { // not first child
            pkey = my_node->get_nth_key(curr_ind - 1, true);
            if (child_node->get_total_entries() != 0) {
                ckey = child_node->get_first_key();
                BT_NODE_DBG_ASSERT_LE(pkey.compare(ckey), 0, child_node);
            }
            // BT_NODE_DBG_ASSERT_EQ((is_range_update_req(put_req) || k.compare(pkey) >= 0), true, my_node);
        }
#endif
        if (curr_ind == end_ind) {
            // If we have reached the last index, unlock before traversing down, because we no longer need
            // this lock. Holding this lock will impact performance unncessarily.
            unlock_node(my_node, curlock);
            curlock = locktype_t::NONE;
        }

#ifndef NDEBUG
        if (child_cur_lock == locktype_t::WRITE) {
            BT_NODE_DBG_ASSERT_EQ(child_node->m_trans_hdr.is_lock, true, child_node);
        }
#endif

        ret = do_put(child_node, child_cur_lock, put_req, ind_hint);
        if (ret != btree_status_t::success) { goto out; }

        ++curr_ind;
    }
out:
    if (curlock != locktype_t::NONE) { unlock_node(my_node, curlock); }
    return ret;
    // Warning: Do not access childNode or myNode beyond this point, since it would
    // have been unlocked by the recursive function and it could also been deleted.
}

template < typename K, typename V >
btree_status_t Btree< K, V >::mutate_write_leaf_node(const BtreeNodePtr< K >& my_node, BtreeMutateRequest& req) {
    btree_status_t ret = btree_status_t::success;
    if (is_range_update_req(req)) {
        BtreeRangeUpdateRequest& rureq = to_range_update_req(req);
        BtreeSearchState& search_state = rureq.search_state();
        const BtreeKeyRange& subrange = search_state.current_sub_range();

        static thread_local std::vector< std::pair< K, V > > s_match;
        s_match.clear();
        uint32_t start_ind = 0u, end_ind = 0u;
        my_node->get_all(subrange, UINT32_MAX, start_ind, end_ind, &s_match);

        static thread_local std::vector< pair< K, V > > s_replace_kv;
        std::vector< pair< K, V > >* p_replace_kvs = &s_match;
        if (m_bt_cfg.is_custom_kv()) {
            s_replace_kv.clear();
            // rreq.get_cb_param()->node_version = my_node->get_version();
            // ret = rreq.callback()(s_match, s_replace_kv, rreq.get_cb_param(), subrange);
            ret = custom_kv_select_for_write(my_node->get_version(), s_match, s_replace_kv, subrange, rureq);
            if (ret != btree_status_t::success) { return ret; }
            p_replace_kvs = &s_replace_kv;
        }

        BT_NODE_DBG_ASSERT_LE(start_ind, end_ind, my_node);
        if (s_match.size() > 0) { my_node->remove(start_ind, end_ind); }
        COUNTER_DECREMENT(m_metrics, btree_obj_count, s_match.size());

        for (const auto& [key, value] : *p_replace_kvs) { // insert is based on compare() of BtreeKey
            auto status = my_node->insert(key, value);
            BT_NODE_REL_ASSERT_EQ(status, btree_status_t::success, my_node, "unexpected insert failure");
            COUNTER_INCREMENT(m_metrics, btree_obj_count, 1);
        }

        /* update cursor in intermediate search state */
        rureq.search_state().set_cursor_key< K >(subrange.end_key());
    } else {
        const BtreeSinglePutRequest& sreq = to_single_put_req(req);
        if (!my_node->put(sreq.key(), sreq.value(), sreq.m_put_type, sreq.m_existing_val.get())) {
            ret = btree_status_t::put_failed;
        }
        COUNTER_INCREMENT(m_metrics, btree_obj_count, 1);
    }

    if (ret == btree_status_t::success) { write_node(my_node, put_req_op_ctx(req)); }
    return ret;
}

/* It split the child if a split is required. It releases lock on parent and child_node in case of failure */
template < typename K, typename V >
btree_status_t Btree< K, V >::check_and_split_node(const BtreeNodePtr< K >& my_node, BtreeMutateRequest& req,
                                                   int ind_hint, const BtreeNodePtr< K >& child_node,
                                                   locktype_t& curlock, locktype_t& child_curlock, int child_ind,
                                                   bool& split_occured) {
    split_occured = false;
    K split_key;
    btree_status_t ret = btree_status_t::success;
    auto child_lock_type = child_curlock;
    auto none_lock_type = locktype_t::NONE;

#ifdef _PRERELEASE
    boost::optional< int > time;
    if (child_node->is_leaf()) {
        time = homestore_flip->get_test_flip< int >("btree_delay_and_split_leaf", child_node->get_total_entries());
    } else {
        time = homestore_flip->get_test_flip< int >("btree_delay_and_split", child_node->get_total_entries());
    }
    if (time && child_node->get_total_entries() > 2) {
        std::this_thread::sleep_for(std::chrono::microseconds{time.get()});
    } else
#endif
    {
        if (!is_split_needed(child_node, m_bt_cfg, req)) { return ret; }
    }

    /* Split needed */
    if (is_range_update_req(req)) {
        /* In case of range update we might split multiple childs of a parent in a single
         * iteration which result into less space in the parent node.
         */
#ifdef _PRERELEASE
        if (homestore_flip->test_flip("btree_parent_node_full")) {
            ret = btree_status_t::retry;
            goto out;
        }
#endif
        if (is_split_needed(my_node, m_bt_cfg, req)) {
            // restart from root
            ret = btree_status_t::retry;
            bt_thread_vars()->force_split_node = my_node; // On retry force split the my_node
            goto out;
        }
    }

    // Time to split the child, but we need to convert parent to write lock
    ret = upgrade_node(my_node, child_node, put_req_op_ctx(req), curlock, child_curlock);
    if (ret != btree_status_t::success) {
        BT_NODE_LOG(DEBUG, my_node, "Upgrade of node lock failed, retrying from root");
        BT_NODE_LOG_ASSERT_EQ(curlock, locktype_t::NONE, my_node);
        goto out;
    }
    BT_NODE_LOG_ASSERT_EQ(child_curlock, child_lock_type, my_node);
    BT_NODE_LOG_ASSERT_EQ(curlock, locktype_t::WRITE, my_node);

    // We need to upgrade the child to WriteLock
    ret = upgrade_node(child_node, nullptr, put_req_op_ctx(req), child_curlock, none_lock_type);
    if (ret != btree_status_t::success) {
        BT_NODE_LOG(DEBUG, child_node, "Upgrade of child node lock failed, retrying from root");
        BT_NODE_LOG_ASSERT_EQ(child_curlock, locktype_t::NONE, child_node);
        goto out;
    }
    BT_NODE_LOG_ASSERT_EQ(none_lock_type, locktype_t::NONE, my_node);
    BT_NODE_LOG_ASSERT_EQ(child_curlock, locktype_t::WRITE, child_node);

    // Real time to split the node and get point at which it was split
    ret = split_node(my_node, child_node, child_ind, &split_key, false /* root_split */, put_req_op_ctx(req));
    if (ret != btree_status_t::success) { goto out; }

    // After split, retry search and walk down.
    unlock_node(child_node, locktype_t::WRITE);
    child_curlock = locktype_t::NONE;
    COUNTER_INCREMENT(m_metrics, btree_split_count, 1);
    split_occured = true;

out:
    if (ret != btree_status_t::success) {
        if (curlock != locktype_t::NONE) {
            unlock_node(my_node, curlock);
            curlock = locktype_t::NONE;
        }

        if (child_curlock != locktype_t::NONE) {
            unlock_node(child_node, child_curlock);
            child_curlock = locktype_t::NONE;
        }
    }
    return ret;
}

template < typename K, typename V >
btree_status_t Btree< K, V >::check_split_root(BtreeMutateRequest& req) {
    K split_key;
    BtreeNodePtr< K > child_node = nullptr;
    btree_status_t ret = btree_status_t::success;

    m_btree_lock.lock();
    BtreeNodePtr< K > root;

    ret = read_and_lock_root(m_root_node_id, root, locktype_t::WRITE, locktype_t::WRITE, put_req_op_ctx(req));
    if (ret != btree_status_t::success) { goto done; }

    if (!is_split_needed(root, m_bt_cfg, req)) {
        unlock_node(root, locktype_t::WRITE);
        goto done;
    }

    // Create a new child node and split them
    child_node = alloc_interior_node();
    if (child_node == nullptr) {
        ret = btree_status_t::space_not_avail;
        unlock_node(root, locktype_t::WRITE);
        goto done;
    }

    /* it swap the data while keeping the nodeid same */
    swap_node(root, child_node, put_req_op_ctx(req));
    write_node(child_node, put_req_op_ctx(req));

    BT_NODE_LOG(DEBUG, root, "Root node is full, swapping contents with child_node {} and split that",
                child_node->get_node_id());

    BT_NODE_DBG_ASSERT_EQ(root->get_total_entries(), 0, root);
    ret = split_node(root, child_node, root->get_total_entries(), &split_key, true, put_req_op_ctx(req));
    BT_NODE_DBG_ASSERT_EQ(m_root_node_id, root->get_node_id(), root);

    if (ret != btree_status_t::success) {
        swap_node(child_node, root, put_req_op_ctx(req));
        write_node(child_node, put_req_op_ctx(req));
    }

    /* unlock child node */
    unlock_node(root, locktype_t::WRITE);

    if (ret == btree_status_t::success) { COUNTER_INCREMENT(m_metrics, btree_depth, 1); }
done:
    m_btree_lock.unlock();
    return ret;
}

template < typename K, typename V >
btree_status_t Btree< K, V >::split_node(const BtreeNodePtr< K >& parent_node, const BtreeNodePtr< K >& child_node,
                                         uint32_t parent_ind, BtreeKey* out_split_key, bool root_split, void* context) {
    BtreeNodeInfo ninfo;
    BtreeNodePtr< K > child_node1 = child_node;
    BtreeNodePtr< K > child_node2 = child_node1->is_leaf() ? alloc_leaf_node() : alloc_interior_node();

    if (child_node2 == nullptr) { return (btree_status_t::space_not_avail); }

    btree_status_t ret = btree_status_t::success;

    child_node2->set_next_bnode(child_node1->next_bnode());
    child_node1->set_next_bnode(child_node2->get_node_id());
    uint32_t child1_filled_size = BtreeNode< K >::node_area_size(m_bt_cfg) - child_node1->get_available_size(m_bt_cfg);

    auto split_size = m_bt_cfg.split_size(child1_filled_size);
    uint32_t res = child_node1->move_out_to_right_by_size(m_bt_cfg, *child_node2, split_size);

    BT_NODE_REL_ASSERT_GT(res, 0, child_node1,
                          "Unable to split entries in the child node"); // means cannot split entries
    BT_NODE_DBG_ASSERT_GT(child_node1->get_total_entries(), 0, child_node1);

    // In an unlikely case where parent node has no room to accomodate the child key, we need to un-split and then
    // free up the new node. This situation could happen on variable key, where the key max size is purely
    // an estimation. This logic allows the max size to be declared more optimistically than say 1/4 of node
    // which will have substatinally large number of splits and performance constraints.
    if (out_split_key->serialized_size() > parent_node->get_available_size(m_bt_cfg)) {
        uint32_t move_in_res = child_node1->move_in_from_right_by_size(m_bt_cfg, *child_node2, split_size);
        BT_NODE_REL_ASSERT_EQ(move_in_res, res, child_node1,
                              "The split key size is more than estimated parent available space, but when revert is "
                              "attempted it fails. Continuing can cause data loss, so crashing");
        free_node(child_node2, context);

        // Mark the parent_node itself to be split upon next retry.
        bt_thread_vars()->force_split_node = parent_node;
        return btree_status_t::retry;
    }

    // Update the existing parent node entry to point to second child ptr.
    bool edge_split = (parent_ind == parent_node->get_total_entries());
    ninfo.set_bnode_id(child_node2->get_node_id());
    parent_node->update(parent_ind, ninfo);

    // Insert the last entry in first child to parent node
    *out_split_key = child_node1->get_last_key();
    ninfo.set_bnode_id(child_node1->get_node_id());

    // If key is extent then we always insert the tail portion of the extent key in the parent node
    if (out_split_key->is_extent_key()) {
        K split_tail_key{out_split_key->serialize_tail(), true};
        parent_node->insert(split_tail_key, ninfo);
    } else {
        parent_node->insert(*out_split_key, ninfo);
    }

    BT_NODE_DBG_ASSERT_GT(child_node2->get_first_key().compare(*out_split_key), 0, child_node2);
    BT_NODE_LOG(DEBUG, parent_node, "Split child_node={} with new_child_node={}, split_key={}",
                child_node1->get_node_id(), child_node2->get_node_id(), out_split_key->to_string());

    split_node_precommit(parent_node, child_node1, child_node2, root_split, edge_split, context);

#if 0
    if (BtreeStoreType == btree_store_type::SSD_BTREE) {
        auto j_iob = btree_store_t::make_journal_entry(journal_op::BTREE_SPLIT, root_split, bcp,
                                                       {parent_node->get_node_id(), parent_node->get_gen()});
        btree_store_t::append_node_to_journal(
            j_iob, (root_split ? bt_journal_node_op::creation : bt_journal_node_op::inplace_write), child_node1, bcp,
            out_split_end_key.get_blob());

        // For root split or split around the edge, we don't write the key, which will cause replay to insert
        // edge
        if (edge_split) {
            btree_store_t::append_node_to_journal(j_iob, bt_journal_node_op::creation, child_node2, bcp);
        } else {
            K child2_pkey;
            parent_node->get_nth_key(parent_ind, &child2_pkey, true);
            btree_store_t::append_node_to_journal(j_iob, bt_journal_node_op::creation, child_node2, bcp,
                                                  child2_pkey.get_blob());
        }
        btree_store_t::write_journal_entry(m_btree_store.get(), bcp, j_iob);
    }
#endif

    // we write right child node, than left and than parent child
    write_node(child_node2, nullptr, context);
    write_node(child_node1, child_node2, context);
    write_node(parent_node, child_node1, context);

    // NOTE: Do not access parentInd after insert, since insert would have
    // shifted parentNode to the right.
    return ret;
}

template < typename K, typename V >
bool Btree< K, V >::is_split_needed(const BtreeNodePtr< K >& node, const BtreeConfig& cfg,
                                    BtreeMutateRequest& req) const {
    if (bt_thread_vars()->force_split_node && (bt_thread_vars()->force_split_node == node)) {
        bt_thread_vars()->force_split_node = nullptr;
        return true;
    }

    int64_t size_needed = 0;
    if (!node->is_leaf()) { // if internal node, size is atmost one additional entry, size of K/V
        size_needed = K::get_estimate_max_size() + BtreeNodeInfo::get_fixed_size() + node->get_record_size();
    } else if (is_range_update_req(req)) {
        /*
         * If there is an overlap then we can add (n + 1) more keys :- one in the front, one in the tail and
         * other in between match entries (n - 1).
         */
        static thread_local std::vector< std::pair< K, V > > s_match;
        s_match.clear();
        uint32_t start_ind = 0, end_ind = 0;
        auto& rureq = to_range_update_req(req);
        node->get_all(rureq.input_range(), UINT32_MAX, start_ind, end_ind, &s_match);

        size_needed = compute_range_put_needed_size(s_match, (const V&)rureq.m_newval) +
            ((s_match.size() + 1) * (K::get_estimate_max_size() + node->get_record_size()));
    } else {
        auto& sreq = to_single_put_req(req);

        // leaf node,
        // NOTE : size_needed is just an guess here. Actual implementation of Mapping key/value can have
        // specific logic which determines of size changes on insert or update.
        auto [found, idx] = node->find(sreq.key(), nullptr, false);
        if (!found) { // We need to insert, this newly. Find out if we have space for value.
            size_needed = sreq.key().serialized_size() + sreq.value().serialized_size() + node->get_record_size();
        } else {
            // Its an update, see how much additional space needed
            V existing_val;
            node->get_nth_value(idx, &existing_val, false);
            size_needed = compute_single_put_needed_size(existing_val, (const V&)sreq.value()) +
                sreq.key().serialized_size() + node->get_record_size();
        }
    }
    int64_t alreadyFilledSize = BtreeNode< K >::node_area_size(cfg) - node->get_available_size(cfg);
    return (alreadyFilledSize + size_needed >= BtreeNode< K >::ideal_fill_size(cfg));
}

template < typename K, typename V >
int64_t Btree< K, V >::compute_single_put_needed_size(const V& current_val, const V& new_val) const {
    return new_val.serialized_size() - current_val.serialized_size();
}

template < typename K, typename V >
int64_t Btree< K, V >::compute_range_put_needed_size(const std::vector< std::pair< K, V > >& existing_kvs,
                                                     const V& new_val) const {
    return new_val.serialized_size() * existing_kvs.size();
}

template < typename K, typename V >
btree_status_t
Btree< K, V >::custom_kv_select_for_write(uint8_t node_version, const std::vector< std::pair< K, V > >& match_kv,
                                          std::vector< std::pair< K, V > >& replace_kv, const BtreeKeyRange& range,
                                          const BtreeRangeUpdateRequest& rureq) const {
    for (const auto& [k, v] : match_kv) {
        replace_kv.push_back(std::make_pair(k, (V&)rureq.m_newval));
    }
    return btree_status_t::success;
}

template < typename K, typename V >
btree_status_t Btree< K, V >::get_start_and_end_ind(const BtreeNodePtr< K >& node, BtreeMutateRequest& req,
                                                    int& start_ind, int& end_ind) {
    btree_status_t ret = btree_status_t::success;
    if (is_range_update_req(req)) {
        /* just get start/end index from get_all. We don't release the parent lock until this
         * key range is not inserted from start_ind to end_ind.
         */
        node->template get_all< V >(to_range_update_req(req).input_range(), UINT32_MAX, (uint32_t&)start_ind,
                                    (uint32_t&)end_ind);
    } else {
        auto [found, idx] = node->find(to_single_put_req(req).key(), nullptr, true);
        ASSERT_IS_VALID_INTERIOR_CHILD_INDX(found, idx, node);
        end_ind = start_ind = (int)idx;
    }

    if (start_ind > end_ind) {
        BT_NODE_LOG_ASSERT(false, node, "start ind {} greater than end ind {}", start_ind, end_ind);
        ret = btree_status_t::retry;
    }
    return ret;
}

} // namespace btree
} // namespace sisl
