#pragma once
#include "btree.hpp"

namespace sisl {
namespace btree {
template < typename K, typename V >
btree_status_t Btree< K, V >::do_remove(const BtreeNodePtr< K >& my_node, locktype_t curlock,
                                        BtreeRemoveRequest& rreq) {
    btree_status_t ret = btree_status_t::success;
    if (my_node->is_leaf()) {
        BT_NODE_DBG_ASSERT_EQ(curlock, locktype_t::WRITE, my_node);

#ifndef NDEBUG
        my_node->validate_key_order();
#endif
        bool is_found;

        if (is_remove_any_request(rreq)) {
            is_found = my_node->remove_any(rreq.m_range, rreq.m_outkey.get(), rreq.m_outval.get());
        } else {
            is_found = my_node->remove_one(rreq.key(), rreq.m_outkey.get(), rreq.m_outval.get());
        }
#ifndef NDEBUG
        my_node->validate_key_order();
#endif
        if (is_found) {
            write_node(my_node, nullptr, remove_req_op_ctx(rreq));
            COUNTER_DECREMENT(m_metrics, btree_obj_count, 1);
        }

        unlock_node(my_node, curlock);
        return is_found ? btree_status_t::success : btree_status_t::not_found;
    }

retry:
    locktype_t child_cur_lock = locktype_t::NONE;
    bool found;
    uint32_t ind;

    // TODO: Range Delete support needs to be added here
    // Get the childPtr for given key.
    if (is_remove_any_request(rreq)) {
        std::tie(found, ind) = my_node->find(to_remove_any_req(rreq).m_range.start_key(), &child_info, true);
    } else {
        std::tie(found, ind) = my_node->find(to_single_remove_req(rreq).key(), &child_info, true);
    }

    ASSERT_IS_VALID_INTERIOR_CHILD_INDX(found, ind, my_node);

    BtreeNodeInfo child_info;
    BtreeNodePtr< K > child_node;
    ret = get_child_and_lock_node(my_node, ind, child_info, child_node, locktype_t::READ, locktype_t::WRITE);
    if (ret != btree_status_t::success) {
        unlock_node(my_node, curlock);
        return ret;
    }

    // Check if child node is minimal.
    child_cur_lock = child_node->is_leaf() ? locktype_t::WRITE : locktype_t::READ;
    if (child_node->is_merge_needed(m_bt_cfg)) {
        // If we are unable to upgrade the node, ask the caller to retry.
        ret = upgrade_node(my_node, child_node, curlock, child_cur_lock);
        if (ret != btree_status_t::success) {
            BT_NODE_DBG_ASSERT_EQ(curlock, locktype_t::NONE, my_node)
            return ret;
        }
        BT_NODE_DBG_ASSERT_EQ(curlock, locktype_t::WRITE, my_node);

        uint32_t node_end_indx =
            my_node->has_valid_edge() ? my_node->get_total_entries() : my_node->get_total_entries() - 1;
        uint32_t end_ind = (ind + HS_DYNAMIC_CONFIG(btree->max_nodes_to_rebalance)) < node_end_indx
            ? (ind + HS_DYNAMIC_CONFIG(btree->max_nodes_to_rebalance))
            : node_end_indx;
        if (end_ind > ind) {
            // It is safe to unlock child without upgrade, because child node would not be deleted, since its
            // parent (myNode) is being write locked by this thread. In fact upgrading would be a problem, since
            // this child might be a middle child in the list of indices, which means we might have to lock one
            // in left against the direction of intended locking (which could cause deadlock).
            unlock_node(child_node, child_cur_lock);
            auto result = merge_nodes(my_node, ind, end_ind);
            if (result != btree_status_t::success && result != btree_status_t::merge_not_required) {
                // write or read failed
                unlock_node(my_node, curlock);
                return ret;
            }
            if (result == btree_status_t::success) { COUNTER_INCREMENT(m_metrics, btree_merge_count, 1); }
            goto retry;
        }
    }

#ifndef NDEBUG
    if (ind != my_node->get_total_entries() && child_node->get_total_entries()) { // not edge
        BT_NODE_DBG_ASSERT_LE(child_node->get_last_key().compare(my_node->get_nth_key(ind, false)), 0, my_node);
    }

    if (ind > 0 && child_node->get_total_entries()) { // not first child
        BT_NODE_DBG_ASSERT_LT(child_node->get_first_key().compare(my_node->get_nth_key(ind - 1, false)), 0, my_node);
    }
#endif

    unlock_node(my_node, curlock);
    return (do_remove(child_node, child_cur_lock, rreq));

    // Warning: Do not access childNode or myNode beyond this point, since it would
    // have been unlocked by the recursive function and it could also been deleted.
}

template < typename K, typename V >
btree_status_t Btree< K, V >::check_collapse_root(void* context) {
    BtreeNodePtr< K > child_node = nullptr;
    btree_status_t ret = btree_status_t::success;
    std::vector< BtreeNodePtr< K > > old_nodes;
    std::vector< BtreeNodePtr< K > > new_nodes;

    m_btree_lock.lock();
    BtreeNodePtr< K > root;

    ret = read_and_lock_root(m_root_node_id, root, locktype_t::WRITE, locktype_t::WRITE, context);
    if (ret != btree_status_t::success) { goto done; }

    if (root->get_total_entries() != 0 || root->is_leaf() /*some other thread collapsed root already*/) {
        unlock_node(root, locktype_t::WRITE);
        goto done;
    }

    BT_NODE_DBG_ASSERT_EQ(root->has_valid_edge(), true, root);
    ret = read_node(root->get_edge_id(), child_node);
    if (child_node == nullptr) {
        unlock_node(root, locktype_t::WRITE);
        goto done;
    }

    // Elevate the edge child as root.
    swap_node(root, child_node, context);
    write_node(root, context);
    BT_NODE_DBG_ASSERT_EQ(m_root_node_id, root->get_node_id(), root);
    old_nodes.push_back(child_node);

    static thread_local std::vector< BtreeNodePtr< K > > s_nodes;
    s_nodes.clear();
    s_nodes.push_back(child_node);
    merge_node_precommit(true, nullptr, 0, root, &s_nodes, nullptr, context);

    unlock_node(root, locktype_t::WRITE);
    free_node(child_node, context);

    if (ret == btree_status_t::success) { COUNTER_DECREMENT(m_metrics, btree_depth, 1); }
done:
    m_btree_lock.unlock();
    return ret;
}

template < typename K, typename V >
btree_status_t Btree< K, V >::merge_nodes(const BtreeNodePtr< K >& parent_node, uint32_t start_indx, uint32_t end_indx,
                                          void* context) {
    btree_status_t ret = btree_status_t::merge_failed;
    std::vector< BtreeNodePtr< K > > child_nodes;
    std::vector< BtreeNodePtr< K > > old_nodes;
    std::vector< BtreeNodePtr< K > > replace_nodes;
    std::vector< BtreeNodePtr< K > > new_nodes;
    std::vector< BtreeNodePtr< K > > deleted_nodes;
    BtreeNodePtr< K > left_most_node;
    K last_pkey; // last key of parent node
    bool last_pkey_valid = false;
    uint32_t balanced_size;
    BtreeNodePtr< K > merge_node;
    K last_ckey; // last key in child
    uint32_t parent_insert_indx = start_indx;
#ifndef NDEBUG
    uint32_t total_child_entries = 0;
    uint32_t new_entries = 0;
    K last_debug_ckey;
    K new_last_debug_ckey;
    BtreeNodePtr< K > last_node;
#endif
    /* Try to take a lock on all nodes participating in merge*/
    for (auto indx = start_indx; indx <= end_indx; ++indx) {
        if (indx == parent_node->get_total_entries()) {
            BT_NODE_LOG_ASSERT(parent_node->has_valid_edge(), parent_node,
                               "Assertion failure, expected valid edge for parent_node: {}");
        }

        BtreeNodeInfo child_info;
        parent_node->get(indx, &child_info, false /* copy */);

        BtreeNodePtr< K > child;
        ret = read_and_lock_node(child_info.bnode_id(), child, locktype_t::WRITE, locktype_t::WRITE, bcp);
        if (ret != btree_status_t::success) { goto out; }
        BT_NODE_LOG_ASSERT_EQ(child->is_valid_node(), true, child);

        /* check if left most node has space */
        if (indx == start_indx) {
            balanced_size = m_bt_cfg.ideal_fill_size();
            left_most_node = child;
            if (left_most_node->get_occupied_size(m_bt_cfg) > balanced_size) {
                /* first node doesn't have any free space. we can exit now */
                ret = btree_status_t::merge_not_required;
                goto out;
            }
        } else {
            bool is_allocated = true;
            /* pre allocate the new nodes. We will free the nodes which are not in use later */
            auto new_node = alloc_node(child->is_leaf(), is_allocated, child);
            if (is_allocated) {
                /* we are going to allocate new blkid of all the nodes except the first node.
                 * Note :- These blkids will leak if we fail or crash before writing entry into
                 * journal.
                 */
                old_nodes.push_back(child);
                COUNTER_INCREMENT_IF_ELSE(m_metrics, child->is_leaf(), btree_leaf_node_count, btree_int_node_count, 1);
            }
            /* Blk IDs can leak if it crash before writing it to a journal */
            if (new_node == nullptr) {
                ret = btree_status_t::space_not_avail;
                goto out;
            }
            new_nodes.push_back(new_node);
        }
#ifndef NDEBUG
        total_child_entries += child->get_total_entries();
        child->get_last_key(&last_debug_ckey);
#endif
        child_nodes.push_back(child);
    }

    if (end_indx != parent_node->get_total_entries()) {
        /* If it is not edge we always preserve the last key in a given merge group of nodes.*/
        parent_node->get_nth_key(end_indx, &last_pkey, true);
        last_pkey_valid = true;
    }

    merge_node = left_most_node;
    /* We can not fail from this point. Nodes will be modified in memory. */
    for (uint32_t i = 0; i < new_nodes.size(); ++i) {
        auto occupied_size = merge_node->get_occupied_size(m_bt_cfg);
        if (occupied_size < balanced_size) {
            uint32_t pull_size = balanced_size - occupied_size;
            merge_node->move_in_from_right_by_size(m_bt_cfg, new_nodes[i], pull_size);
            if (new_nodes[i]->get_total_entries() == 0) {
                /* this node is freed */
                deleted_nodes.push_back(new_nodes[i]);
                continue;
            }
        }

        /* update the last key of merge node in parent node */
        K last_ckey; // last key in child
        merge_node->get_last_key(&last_ckey);
        BtreeNodeInfo ninfo(merge_node->get_node_id());
        parent_node->update(parent_insert_indx, last_ckey, ninfo);
        ++parent_insert_indx;

        merge_node->set_next_bnode(new_nodes[i]->get_node_id()); // link them
        merge_node = new_nodes[i];
        if (merge_node != left_most_node) {
            /* left most node is not replaced */
            replace_nodes.push_back(merge_node);
        }
    }

    /* update the latest merge node */
    merge_node->get_last_key(&last_ckey);
    if (last_pkey_valid) {
        BT_DBG_ASSERT_CMP(last_ckey.compare(&last_pkey), <=, 0, parent_node);
        last_ckey = last_pkey;
    }

    /* update the last key */
    {
        BtreeNodeInfo ninfo(merge_node->get_node_id());
        parent_node->update(parent_insert_indx, last_ckey, ninfo);
        ++parent_insert_indx;
    }

    /* remove the keys which are no longer used */
    if ((parent_insert_indx) <= end_indx) { parent_node->remove(parent_insert_indx, end_indx); }

    // TODO: Validate if empty child_pkey on last_key or edge has any impact on journal/precommit
    K child_pkey;
    if (start_indx < parent_node->get_total_entries()) {
        child_pkey = parent_node->get_nth_key(start_indx, true);
        BT_NODE_REL_ASSERT_EQ(start_indx, (parent_insert_indx - 1), parent_node, "it should be last index");
    }

    merge_node_precommit(false, parent_node, parent_merge_start_idx, left_most_node, &old_nodes, &replace_nodes,
                         context);

#if 0
    /* write the journal entry */
    if (BtreeStoreType == btree_store_type::SSD_BTREE) {
        auto j_iob = btree_store_t::make_journal_entry(journal_op::BTREE_MERGE, false /* is_root */, bcp,
                                                       {parent_node->get_node_id(), parent_node->get_gen()});
        K child_pkey;
        if (start_indx < parent_node->get_total_entries()) {
            parent_node->get_nth_key(start_indx, &child_pkey, true);
            BT_REL_ASSERT_CMP(start_indx, ==, (parent_insert_indx - 1), parent_node, "it should be last index");
        }
        btree_store_t::append_node_to_journal(j_iob, bt_journal_node_op::inplace_write, left_most_node, bcp,
                                              child_pkey.get_blob());
        for (auto& node : old_nodes) {
            btree_store_t::append_node_to_journal(j_iob, bt_journal_node_op::removal, node, bcp);
        }
        uint32_t insert_indx = 0;
        for (auto& node : replace_nodes) {
            K child_pkey;
            if ((start_indx + insert_indx) < parent_node->get_total_entries()) {
                parent_node->get_nth_key(start_indx + insert_indx, &child_pkey, true);
                BT_REL_ASSERT_CMP((start_indx + insert_indx), ==, (parent_insert_indx - 1), parent_node,
                                      "it should be last index");
            }
            btree_store_t::append_node_to_journal(j_iob, bt_journal_node_op::creation, node, bcp,
                                                  child_pkey.get_blob());
            ++insert_indx;
        }
        BT_REL_ASSERT_CMP((start_indx + insert_indx), ==, parent_insert_indx, parent_node, "it should be same");
        btree_store_t::write_journal_entry(m_btree_store.get(), bcp, j_iob);
    }
#endif

    if (replace_nodes.size() > 0) {
        /* write the right most node */
        write_node(replace_nodes[replace_nodes.size() - 1], nullptr, bcp);
        if (replace_nodes.size() > 1) {
            /* write the middle nodes */
            for (int i = replace_nodes.size() - 2; i >= 0; --i) {
                write_node(replace_nodes[i], replace_nodes[i + 1], bcp);
            }
        }
        /* write the left most node */
        write_node(left_most_node, replace_nodes[0], bcp);
    } else {
        /* write the left most node */
        write_node(left_most_node, nullptr, bcp);
    }

    /* write the parent node */
    write_node(parent_node, left_most_node, bcp);

#ifndef NDEBUG
    for (const auto& n : replace_nodes) {
        new_entries += n->get_total_entries();
    }

    new_entries += left_most_node->get_total_entries();
    BT_DBG_ASSERT_EQ(total_child_entries, new_entries);

    if (replace_nodes.size()) {
        replace_nodes[replace_nodes.size() - 1]->get_last_key(&new_last_debug_ckey);
        last_node = replace_nodes[replace_nodes.size() - 1];
    } else {
        left_most_node->get_last_key(&new_last_debug_ckey);
        last_node = left_most_node;
    }
    if (last_debug_ckey.compare(&new_last_debug_ckey) != 0) {
        LOGINFO("{}", last_node->to_string());
        if (deleted_nodes.size() > 0) { LOGINFO("{}", (deleted_nodes[deleted_nodes.size() - 1]->to_string())); }
        HS_DEBUG_ASSERT(false, "compared failed");
    }
#endif
    /* free nodes. It actually gets freed after cp is completed */
    for (const auto& n : old_nodes) {
        free_node(n, (bcp ? bcp->free_blkid_list : nullptr));
    }
    for (const auto& n : deleted_nodes) {
        free_node(n);
    }
    ret = btree_status_t::success;
out:
#ifndef NDEBUG
    uint32_t freed_entries = deleted_nodes.size();
    uint32_t scan_entries = end_indx - start_indx - freed_entries + 1;
    for (uint32_t i = 0; i < scan_entries; ++i) {
        if (i < (scan_entries - 1)) { validate_sanity_next_child(parent_node, (uint32_t)start_indx + i); }
        validate_sanity_child(parent_node, (uint32_t)start_indx + i);
    }
#endif
    // Loop again in reverse order to unlock the nodes. freeable nodes need to be unlocked and freed
    for (uint32_t i = child_nodes.size() - 1; i != 0; i--) {
        unlock_node(child_nodes[i], locktype_t::WRITE);
    }
    unlock_node(child_nodes[0], locktype_t::WRITE);
    if (ret != btree_status_t::success) {
        /* free the allocated nodes */
        for (const auto& n : new_nodes) {
            free_node(n);
        }
    }
    return ret;
}
} // namespace btree
} // namespace sisl
