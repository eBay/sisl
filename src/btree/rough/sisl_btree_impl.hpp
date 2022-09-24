#pragma once

namespace sisl {
namespace btree {
template < typename K, typename V >
class BtreeImpl {
protected:
    template < typename K, typename V >
    btree_status_t Btree< K, V >::post_order_traversal(locktype_t ltype,
                                                       const std::function< void(const BtreeNodePtr< K >&) >& cb) {
        BtreeNodePtr< K > root;
        btree_status_t ret = read_and_lock_root(m_root_node, root, acq_lock, acq_lock, nullptr);
        if (ret != btree_status_t::success) {
            m_btree_lock.unlock();
            return ret;
        }

        post_order_traversal(root, ltype, cb);
    }

    template < typename K, typename V >
    btree_status_t Btree< K, V >::post_order_traversal(const BtreeNodePtr< K >& node, locktype_t ltype,
                                                       const auto& cb) {
        homeds::thread::locktype acq_lock = homeds::thread::LOCKTYPE_WRITE;
        uint32_t i = 0;
        btree_status_t ret = btree_status_t::success;

        if (!node->is_leaf()) {
            BtreeNodeInfo child_info;
            while (i <= node->get_total_entries()) {
                if (i == node->get_total_entries()) {
                    if (!node->has_valid_edge()) { break; }
                    child_info.set_bnode_id(node->get_edge_id());
                } else {
                    child_info = node->get(i, false /* copy */);
                }

                BtreeNodePtr< K > child;
                ret = read_and_lock_child(child_info.bnode_id(), child, node, i, acq_lock, acq_lock, nullptr);
                if (ret != btree_status_t::success) { return ret; }
                ret = post_order_traversal(child, cb);
                unlock_node(child, acq_lock);
                ++i;
            }
        }

        if (ret != btree_status_t::success) { return ret; }
        cb(node);
        return ret;
    }

    btree_status_t put_internal(const BtreeMutateRequest& put_req) {
        COUNTER_INCREMENT(m_metrics, btree_write_ops_count, 1);
        locktype acq_lock = locktype::READ;
        int ind = -1;
        bool is_leaf = false;

        // THIS_BT_LOG(INFO, base, , "Put called for key = {}, value = {}", k.to_string(), v.to_string());

        m_btree_lock.read_lock();

        btree_status_t ret = btree_status_t::success;
    retry:

#ifndef NDEBUG
        check_lock_debug();
#endif
        BT_LOG_ASSERT_CMP(rd_locked_nodes.size(), ==, 0, );
        BT_LOG_ASSERT_CMP(wr_locked_nodes.size(), ==, 0, );

        BtreeNodePtr< K > root;
        ret = read_and_lock_root(m_root_node, root, acq_lock, acq_lock);
        if (ret != btree_status_t::success) { goto out; }
        is_leaf = root->is_leaf();

        if (root->is_split_needed(m_bt_cfg, put_req)) {
            // Time to do the split of root.
            unlock_node(root, acq_lock);
            m_btree_lock.unlock();
            ret = check_split_root(put_req);
            BT_LOG_ASSERT_CMP(rd_locked_nodes.size(), ==, 0, );
            BT_LOG_ASSERT_CMP(wr_locked_nodes.size(), ==, 0, );

            // We must have gotten a new root, need to start from scratch.
            m_btree_lock.read_lock();

            if (ret != btree_status_t::success) {
                LOGERROR("root split failed btree name {}", m_bt_cfg.get_name());
                goto out;
            }

            goto retry;
        } else if ((is_leaf) && (acq_lock != homeds::thread::LOCKTYPE_WRITE)) {
            // Root is a leaf, need to take write lock, instead of read, retry
            unlock_node(root, acq_lock);
            acq_lock = homeds::thread::LOCKTYPE_WRITE;
            goto retry;
        } else {
            K subrange_start_key, subrange_end_key;
            bool start_incl = false, end_incl = false;
            if (is_range_update_req(put_req)) {
                to_range_update_req(put_req)->get_input_range().copy_start_end_blob(subrange_start_key, start_incl,
                                                                                    subrange_end_key, end_incl);
            }
            BtreeSearchRange subrange(subrange_start_key, start_incl, subrange_end_key, end_incl);
            ret = do_put(root, acq_lock, put_req, ind, subrange);
            if (ret == btree_status_t::retry) {
                // Need to start from top down again, since there is a race between 2 inserts or deletes.
                acq_lock = homeds::thread::locktype_t::READ;
                THIS_BT_LOG(TRACE, btree_generics, , "retrying put operation");
                BT_LOG_ASSERT_CMP(rd_locked_nodes.size(), ==, 0, );
                BT_LOG_ASSERT_CMP(wr_locked_nodes.size(), ==, 0, );
                goto retry;
            }
        }

    out:
        m_btree_lock.unlock();
#ifndef NDEBUG
        check_lock_debug();
#endif
        if (ret != btree_status_t::success && ret != btree_status_t::fast_path_not_possible &&
            ret != btree_status_t::cp_mismatch) {
            THIS_BT_LOG(ERROR, base, , "btree put failed {}", ret);
            COUNTER_INCREMENT(m_metrics, write_err_cnt, 1);
        }

        return ret;
    }

    btree_status_t do_get(const BtreeNodePtr< K >& my_node, const BtreeSearchRange& range, BtreeKey* outkey,
                          BtreeValue* outval) const {
        btree_status_t ret = btree_status_t::success;
        bool is_child_lock = false;
        homeds::thread::locktype child_locktype;

        if (my_node->is_leaf()) {
            auto result = my_node->find(range, outkey, outval);
            if (result.found) {
                ret = btree_status_t::success;
            } else {
                ret = btree_status_t::not_found;
            }
            unlock_node(my_node, homeds::thread::locktype::locktype_t::READ);
            return ret;
        }

        BtreeNodeInfo child_info;
        auto result = my_node->find(range, nullptr, &child_info);
        ASSERT_IS_VALID_INTERIOR_CHILD_INDX(result, my_node);

        BtreeNodePtr< K > child_node;
        child_locktype = homeds::thread::locktype_t::READ;
        ret = read_and_lock_child(child_info.bnode_id(), child_node, my_node, result.end_of_search_index,
                                  child_locktype, child_locktype, nullptr);
        if (ret != btree_status_t::success) { goto out; }

        unlock_node(my_node, homeds::thread::locktype::locktype_t::READ);

        return (do_get(child_node, range, outkey, outval));
    out:
        unlock_node(my_node, homeds::thread::locktype::locktype_t::READ);
        return ret;
    }

    btree_status_t do_remove(const BtreeNodePtr< K >& my_node, locktype curlock, const BtreeSearchRange& range,
                             BtreeKey* outkey, BtreeValue* outval) {
        btree_status_t ret = btree_status_t::success;
        if (my_node->is_leaf()) {
            BT_DEBUG_ASSERT_CMP(curlock, ==, LOCKTYPE_WRITE, my_node);

#ifndef NDEBUG
            my_node->validate_key_order();
#endif
            bool is_found = my_node->remove_one(range, outkey, outval);
#ifndef NDEBUG
            my_node->validate_key_order();
#endif
            if (is_found) {
                write_node(my_node);
                COUNTER_DECREMENT(m_metrics, btree_obj_count, 1);
            }

            unlock_node(my_node, curlock);
            return is_found ? btree_status_t::success : btree_status_t::not_found;
        }

    retry:
        locktype child_cur_lock = LOCKTYPE_NONE;

        /* range delete is not supported yet */
        // Get the childPtr for given key.
        auto [found, ind] = my_node->find(range, nullptr, nullptr);
        ASSERT_IS_VALID_INTERIOR_CHILD_INDX(result, my_node);

        BtreeNodeInfo child_info;
        BtreeNodePtr< K > child_node;
        ret = get_child_and_lock_node(my_node, ind, child_info, child_node, locktype_t::READ, LOCKTYPE_WRITE);
        if (ret != btree_status_t::success) {
            unlock_node(my_node, curlock);
            return ret;
        }

        // Check if child node is minimal.
        child_cur_lock = child_node->is_leaf() ? LOCKTYPE_WRITE : locktype_t::READ;
        if (child_node->is_merge_needed(m_bt_cfg)) {
            // If we are unable to upgrade the node, ask the caller to retry.
            ret = upgrade_node(my_node, child_node, curlock, child_cur_lock, bcp);
            if (ret != btree_status_t::success) {
                BT_DEBUG_ASSERT_CMP(curlock, ==, locktype::NONE, my_node)
                return ret;
            }
            BT_DEBUG_ASSERT_CMP(curlock, ==, locktype::WRITE, my_node);

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
            const auto ckey = child_node->get_last_key();
            const auto pkey = my_node->get_nth_key(ind, true);
            BT_DEBUG_ASSERT_CMP(ckey.compare(&pkey), <=, 0, my_node);
        }

        if (ind > 0 && child_node->get_total_entries()) { // not first child
            const auto ckey = child_node->get_first_key();
            const auto pkey = my_node->get_nth_key(ind - 1, true);
            BT_DEBUG_ASSERT_CMP(pkey.compare(&ckey), <, 0, my_node);
        }
#endif

        unlock_node(my_node, curlock);
        return (do_remove(child_node, child_cur_lock, range, outkey, outval));

        // Warning: Do not access childNode or myNode beyond this point, since it would
        // have been unlocked by the recursive function and it could also been deleted.
    }

private:
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
    btree_status_t do_put(const BtreeNodePtr< K >& my_node, btree::locktype curlock, const BtreeMutateRequest& put_req,
                          int ind_hint, BtreeSearchRange& child_subrange) {
        btree_status_t ret = btree_status_t::success;
        bool unlocked_already = false;
        int curr_ind = -1;

        if (my_node->is_leaf()) {
            /* update the leaf node */
            BT_LOG_ASSERT_CMP(curlock, ==, LOCKTYPE_WRITE, my_node);
            ret = update_leaf_node(my_node, put_req, child_subrange);
            unlock_node(my_node, curlock);
            return ret;
        }

        bool is_any_child_splitted = false;

    retry:
        int start_ind = 0, end_ind = -1;

        /* Get the start and end ind in a parent node for the range updates. For
         * non range updates, start ind and end ind are same.
         */
        ret = get_start_and_end_ind(my_node, put_req, start_ind, end_ind);
        if (ret != btree_status_t::success) { goto out; }

        BT_DEBUG_ASSERT((curlock == locktype_t::READ || curlock == LOCKTYPE_WRITE), my_node, "unexpected locktype {}",
                        curlock);
        curr_ind = start_ind;

        while (curr_ind <= end_ind) { // iterate all matched childrens

#ifdef _PRERELEASE
            if (curr_ind - start_ind > 1 && homestore_flip->test_flip("btree_leaf_node_split")) {
                ret = btree_status_t::retry;
                goto out;
            }
#endif

            homeds::thread::locktype child_cur_lock = homeds::thread::LOCKTYPE_NONE;

            // Get the childPtr for given key.
            BtreeNodeInfo child_info;
            BtreeNodePtr< K > child_node;

            ret = get_child_and_lock_node(my_node, curr_ind, child_info, child_node, locktype_t::READ, LOCKTYPE_WRITE);
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
            child_cur_lock = (child_node->is_leaf()) ? LOCKTYPE_WRITE : locktype_t::READ;

            /* Get subrange if it is a range update */
            K start_key, end_key;
            bool start_incl = false, end_incl = false;
            if (is_range_update_req(put_req) && child_node->is_leaf()) {
                /* We get the subrange only for leaf because this is where we will be inserting keys. In interior
                 * nodes, keys are always propogated from the lower nodes.
                 */
                get_subrange(my_node, put_req, curr_ind, start_key, end_key, start_incl, end_incl);
            }
            BtreeSearchRange subrange(start_key, start_incl, end_key, end_incl);

            /* check if child node is needed to split */
            bool split_occured = false;
            ret = check_and_split_node(my_node, put_req, ind_hint, child_node, curlock, child_cur_lock, curr_ind,
                                       split_occured);
            if (ret != btree_status_t::success) { goto out; }
            if (split_occured) {
                ind_hint = -1; // Since split is needed, hint is no longer valid
                goto retry;
            }

            if (is_range_update_req(put_req) && child_node->is_leaf()) {
                THIS_BT_LOG(DEBUG, btree_structures, my_node, "Subrange:s:{},e:{},c:{},nid:{},edgeid:{},sk:{},ek:{}",
                            start_ind, end_ind, curr_ind, my_node->get_node_id(), my_node->get_edge_id(),
                            subrange.get_start_key()->to_string(), subrange.get_end_key()->to_string());
            }

#ifndef NDEBUG
            K ckey, pkey;
            if (curr_ind != int_cast(my_node->get_total_entries())) { // not edge
                pkey = my_node->get_nth_key(curr_ind, true);
                if (child_node->get_total_entries() != 0) {
                    ckey = child_node->get_last_key();
                    if (!child_node->is_leaf()) {
                        HS_DEBUG_ASSERT_EQ(ckey.compare(pkey), 0);
                    } else {
                        HS_ASSERT_CMP(DEBUG, ckey.compare(pkey), <=, 0);
                    }
                }
                HS_DEBUG_ASSERT_EQ((is_range_update_req(put_req) || k.compare(pkey) <= 0), true);
            }
            if (curr_ind > 0) { // not first child
                pkey = my_node->get_nth_key(curr_ind - 1, true);
                if (child_node->get_total_entries() != 0) {
                    ckey = child_node->get_first_key();
                    HS_ASSERT_CMP(DEBUG, pkey.compare(ckey), <=, 0);
                }
                HS_DEBUG_ASSERT_EQ((is_range_update_req(put_req) || k.compare(pkey) >= 0), true);
            }
#endif
            if (curr_ind == end_ind) {
                // If we have reached the last index, unlock before traversing down, because we no longer need
                // this lock. Holding this lock will impact performance unncessarily.
                unlock_node(my_node, curlock);
                curlock = LOCKTYPE_NONE;
            }

#ifndef NDEBUG
            if (child_cur_lock == homeds::thread::LOCKTYPE_WRITE) {
                HS_DEBUG_ASSERT_EQ(child_node->m_common_header.is_lock, true);
            }
#endif

            ret = do_put(child_node, child_cur_lock, put_req, ind_hint, subrange);
            if (ret != btree_status_t::success) { goto out; }

            curr_ind++;
        }
    out:
        if (curlock != LOCKTYPE_NONE) { unlock_node(my_node, curlock); }
        return ret;
        // Warning: Do not access childNode or myNode beyond this point, since it would
        // have been unlocked by the recursive function and it could also been deleted.
    }

    void get_all_kvs(std::vector< pair< K, V > >& kvs) const {
        // TODO: Isn't it better to do DFS traversal and get kvs instead of collecting all leafs. Its a non-scalable
        // operation.
        static thread_local std::vector< BtreeNodePtr< K > > leaves;
        leaves.clear();
        get_all_leaf_nodes(leaves);

        for (auto& l : leaves) {
            l->get_all_kvs(kvs);
        }
        leaves.clear();
    }

    uint64_t get_btree_node_cnt() const {
        uint64_t cnt = 1; /* increment it for root */
        m_btree_lock.read_lock();
        cnt += get_child_node_cnt(m_root_node);
        m_btree_lock.unlock();
        return cnt;
    }

    uint64_t get_child_node_cnt(bnodeid_t bnodeid) const {
        uint64_t cnt{0};
        BtreeNodePtr< K > node;
        homeds::thread::locktype acq_lock = homeds::thread::locktype::locktype_t::READ;

        if (read_and_lock_node(bnodeid, node, acq_lock, acq_lock, nullptr) != btree_status_t::success) { return cnt; }
        if (!node->is_leaf()) {
            uint32_t i = 0;
            while (i < node->get_total_entries()) {
                BtreeNodeInfo p = node->get(i, false);
                cnt += get_child_node_cnt(p.bnode_id()) + 1;
                ++i;
            }
            if (node->has_valid_edge()) { cnt += get_child_node_cnt(node->get_edge_id()) + 1; }
        }
        unlock_node(node, acq_lock);
        return cnt;
    }

    /*
     * Get all leaf nodes from the read-only tree (CP tree, Snap Tree etc)
     * NOTE: Doesn't take any lock
     */
    void get_all_leaf_nodes(std::vector< BtreeNodePtr< K > >& leaves) const {
        /* TODO: Add a flag to indicate RO tree
         * TODO: Check the flag here
         */
        get_leaf_nodes(m_root_node, leaves);
    }

    // TODO: Remove the locks once we have RO flags
    void get_leaf_nodes(bnodeid_t bnodeid, std::vector< BtreeNodePtr< K > >& leaves) const {
        BtreeNodePtr< K > node;
        if (read_and_lock_node(bnodeid, node, locktype_t::READ, locktype_t::READ, nullptr) != btree_status_t::success) {
            return;
        }

        if (node->is_leaf()) {
            BtreeNodePtr< K > next_node = nullptr;
            leaves.push_back(node);
            while (node->next_bnode() != empty_bnodeid) {
                auto ret =
                    read_and_lock_sibling(node->next_bnode(), next_node, locktype_t::READ, locktype_t::READ, nullptr);
                unlock_node(node, locktype_t::READ);
                HS_DEBUG_ASSERT_EQ(ret, btree_status_t::success);
                if (ret != btree_status_t::success) {
                    LOGERROR("Cannot read sibling node for {}", node);
                    return;
                }
                HS_DEBUG_ASSERT_EQ(next_node->is_leaf(), true);
                leaves.push_back(next_node);
                node = next_node;
            }
            unlock_node(node, locktype_t::READ);
            return;
        }

        HS_ASSERT_CMP(DEBUG, node->get_total_entries(), >, 0);
        if (node->get_total_entries() > 0) {
            BtreeNodeInfo p = node->get(0, false);
            // XXX If we cannot get rid of locks, lock child and release parent here
            get_leaf_nodes(p.bnode_id(), leaves);
        }
        unlock_node(node, locktype_t::READ);
    }

    void to_string(bnodeid_t bnodeid, std::string& buf) const {
        BtreeNodePtr< K > node;

        homeds::thread::locktype acq_lock = homeds::thread::locktype::locktype_t::READ;

        if (read_and_lock_node(bnodeid, node, acq_lock, acq_lock, nullptr) != btree_status_t::success) { return; }
        fmt::format_to(std::back_inserter(buf), "{}\n", node->to_string(true /* print_friendly */));

        if (!node->is_leaf()) {
            uint32_t i = 0;
            while (i < node->get_total_entries()) {
                BtreeNodeInfo p;
                node->get(i, &p, false);
                to_string(p.bnode_id(), buf);
                i++;
            }
            if (node->has_valid_edge()) { to_string(node->get_edge_id(), buf); }
        }
        unlock_node(node, acq_lock);
    }

    /* This function upgrades the node lock and take required steps if things have
     * changed during the upgrade.
     *
     * Inputs:
     * myNode - Node to upgrade
     * childNode - In case childNode needs to be unlocked. Could be nullptr
     * curLock - Input/Output: current lock type
     *
     * Returns - If successfully able to upgrade, return true, else false.
     *
     * About Locks: This function expects the myNode to be locked and if childNode is not nullptr, expects
     * it to be locked too. If it is able to successfully upgrade it continue to retain its
     * old lock. If failed to upgrade, will release all locks.
     */
    btree_status_t upgrade_node(const BtreeNodePtr< K >& my_node, BtreeNodePtr< K > child_node,
                                homeds::thread::locktype& cur_lock, homeds::thread::locktype& child_cur_lock,
                                const btree_cp_ptr& bcp) {
        uint64_t prev_gen;
        btree_status_t ret = btree_status_t::success;
        homeds::thread::locktype child_lock_type = child_cur_lock;

        if (cur_lock == homeds::thread::LOCKTYPE_WRITE) { goto done; }

        prev_gen = my_node->get_gen();
        if (child_node) {
            unlock_node(child_node, child_cur_lock);
            child_cur_lock = locktype::LOCKTYPE_NONE;
        }

#ifdef _PRERELEASE
        {
            auto time = homestore_flip->get_test_flip< uint64_t >("btree_upgrade_delay");
            if (time) { std::this_thread::sleep_for(std::chrono::microseconds{time.get()}); }
        }
#endif
        ret = lock_node_upgrade(my_node, bcp);
        if (ret != btree_status_t::success) {
            cur_lock = locktype::LOCKTYPE_NONE;
            return ret;
        }

        // The node was not changed by anyone else during upgrade.
        cur_lock = homeds::thread::LOCKTYPE_WRITE;

        // If the node has been made invalid (probably by mergeNodes) ask caller to start over again, but before
        // that cleanup or free this node if there is no one waiting.
        if (!my_node->is_valid_node()) {
            unlock_node(my_node, homeds::thread::LOCKTYPE_WRITE);
            cur_lock = locktype::LOCKTYPE_NONE;
            ret = btree_status_t::retry;
            goto done;
        }

        // If node has been updated, while we have upgraded, ask caller to start all over again.
        if (prev_gen != my_node->get_gen()) {
            unlock_node(my_node, cur_lock);
            cur_lock = locktype::LOCKTYPE_NONE;
            ret = btree_status_t::retry;
            goto done;
        }

        if (child_node) {
            ret = lock_and_refresh_node(child_node, child_lock_type, bcp);
            if (ret != btree_status_t::success) {
                unlock_node(my_node, cur_lock);
                cur_lock = locktype::LOCKTYPE_NONE;
                child_cur_lock = locktype::LOCKTYPE_NONE;
                goto done;
            }
            child_cur_lock = child_lock_type;
        }

#ifdef _PRERELEASE
        {
            int is_leaf = 0;

            if (child_node && child_node->is_leaf()) { is_leaf = 1; }
            if (homestore_flip->test_flip("btree_upgrade_node_fail", is_leaf)) {
                unlock_node(my_node, cur_lock);
                cur_lock = locktype::LOCKTYPE_NONE;
                if (child_node) {
                    unlock_node(child_node, child_cur_lock);
                    child_cur_lock = locktype::LOCKTYPE_NONE;
                }
                ret = btree_status_t::retry;
                goto done;
            }
        }
#endif

        BT_DEBUG_ASSERT_CMP(my_node->m_common_header.is_lock, ==, 1, my_node);
    done:
        return ret;
    }

    btree_status_t update_leaf_node(const BtreeNodePtr< K >& my_node, const BtreeKey& k, const BtreeValue& v,
                                    btree_put_type put_type, BtreeValue& existing_val, BtreeUpdateRequest< K, V >* bur,
                                    const btree_cp_ptr& bcp, BtreeSearchRange& subrange) {
        btree_status_t ret = btree_status_t::success;
        if (bur != nullptr) {
            // BT_DEBUG_ASSERT_CMP(bur->callback(), !=, nullptr, my_node); // TODO - range req without
            // callback implementation
            static thread_local std::vector< std::pair< K, V > > s_match;
            s_match.clear();
            int start_ind = 0, end_ind = 0;
            my_node->get_all(bur->get_input_range(), UINT32_MAX, start_ind, end_ind, &s_match);

            static thread_local std::vector< pair< K, V > > s_replace_kv;
            s_replace_kv.clear();
            bur->get_cb_param()->node_version = my_node->get_version();
            ret = bur->callback()(s_match, s_replace_kv, bur->get_cb_param(), subrange);
            if (ret != btree_status_t::success) { return ret; }

            HS_ASSERT_CMP(DEBUG, start_ind, <=, end_ind);
            if (s_match.size() > 0) { my_node->remove(start_ind, end_ind); }
            COUNTER_DECREMENT(m_metrics, btree_obj_count, s_match.size());

            for (const auto& pair : s_replace_kv) { // insert is based on compare() of BtreeKey
                auto status = my_node->insert(pair.first, pair.second);
                BT_RELEASE_ASSERT((status == btree_status_t::success), my_node, "unexpected insert failure");
                COUNTER_INCREMENT(m_metrics, btree_obj_count, 1);
            }

            /* update cursor in input range */
            auto end_key_ptr = const_cast< BtreeKey* >(subrange.get_end_key());
            bur->get_input_range().set_cursor_key(
                end_key_ptr, ([](BtreeKey* end_key) { return std::move(std::make_unique< K >(*((K*)end_key))); }));
            if (homestore::vol_test_run) {
                // sorted check
                for (auto i = 1u; i < my_node->get_total_entries(); i++) {
                    K curKey, prevKey;
                    my_node->get_nth_key(i - 1, &prevKey, false);
                    my_node->get_nth_key(i, &curKey, false);
                    if (prevKey.compare(&curKey) >= 0) {
                        LOGINFO("my_node {}", my_node->to_string());
                        for (const auto& [k, v] : s_match) {
                            LOGINFO("match key {} value {}", k.to_string(), v.to_string());
                        }
                        for (const auto& [k, v] : s_replace_kv) {
                            LOGINFO("replace key {} value {}", k.to_string(), v.to_string());
                        }
                    }
                    BT_RELEASE_ASSERT_CMP(prevKey.compare(&curKey), <, 0, my_node);
                }
            }
        } else {
            if (!my_node->put(k, v, put_type, existing_val)) { ret = btree_status_t::put_failed; }
            COUNTER_INCREMENT(m_metrics, btree_obj_count, 1);
        }

        write_node(my_node, bcp);
        return ret;
    }

    btree_status_t get_start_and_end_ind(const BtreeNodePtr< K >& my_node, BtreeUpdateRequest< K, V >* bur,
                                         const BtreeKey& k, int& start_ind, int& end_ind) {

        btree_status_t ret = btree_status_t::success;
        if (bur != nullptr) {
            /* just get start/end index from get_all. We don't release the parent lock until this
             * key range is not inserted from start_ind to end_ind.
             */
            my_node->get_all(bur->get_input_range(), UINT32_MAX, start_ind, end_ind);
        } else {
            auto result = my_node->find(k, nullptr, nullptr, true, true);
            end_ind = start_ind = result.end_of_search_index;
            ASSERT_IS_VALID_INTERIOR_CHILD_INDX(result, my_node);
        }

        if (start_ind > end_ind) {
            BT_LOG_ASSERT(false, my_node, "start ind {} greater than end ind {}", start_ind, end_ind);
            ret = btree_status_t::retry;
        }
        return ret;
    }

    /* It split the child if a split is required. It releases lock on parent and child_node in case of failure */
    btree_status_t check_and_split_node(const BtreeNodePtr< K >& my_node, BtreeUpdateRequest< K, V >* bur,
                                        const BtreeKey& k, const BtreeValue& v, int ind_hint, btree_put_type put_type,
                                        BtreeNodePtr< K > child_node, homeds::thread::locktype& curlock,
                                        homeds::thread::locktype& child_curlock, int child_ind, bool& split_occured,
                                        const btree_cp_ptr& bcp) {

        split_occured = false;
        K split_key;
        btree_status_t ret = btree_status_t::success;
        auto child_lock_type = child_curlock;
        auto none_lock_type = LOCKTYPE_NONE;

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
            if (!child_node->is_split_needed(m_cfg, k, v, &ind_hint, put_type, bur)) { return ret; }
        }

        /* Split needed */
        if (bur) {

            /* In case of range update we might split multiple childs of a parent in a single
             * iteration which result into less space in the parent node.
             */
#ifdef _PRERELEASE
            if (homestore_flip->test_flip("btree_parent_node_full")) {
                ret = btree_status_t::retry;
                goto out;
            }
#endif
            if (my_node->is_split_needed(m_cfg, k, v, &ind_hint, put_type, bur)) {
                // restart from root
                ret = btree_status_t::retry;
                goto out;
            }
        }

        // Time to split the child, but we need to convert parent to write lock
        ret = upgrade_node(my_node, child_node, curlock, child_curlock, bcp);
        if (ret != btree_status_t::success) {
            THIS_BT_LOG(DEBUG, btree_structures, my_node, "Upgrade of node lock failed, retrying from root");
            BT_LOG_ASSERT_CMP(curlock, ==, homeds::thread::LOCKTYPE_NONE, my_node);
            goto out;
        }
        BT_LOG_ASSERT_CMP(child_curlock, ==, child_lock_type, my_node);
        BT_LOG_ASSERT_CMP(curlock, ==, homeds::thread::LOCKTYPE_WRITE, my_node);

        // We need to upgrade the child to WriteLock
        ret = upgrade_node(child_node, nullptr, child_curlock, none_lock_type, bcp);
        if (ret != btree_status_t::success) {
            THIS_BT_LOG(DEBUG, btree_structures, child_node, "Upgrade of child node lock failed, retrying from root");
            BT_LOG_ASSERT_CMP(child_curlock, ==, homeds::thread::LOCKTYPE_NONE, child_node);
            goto out;
        }
        BT_LOG_ASSERT_CMP(none_lock_type, ==, homeds::thread::LOCKTYPE_NONE, my_node);
        BT_LOG_ASSERT_CMP(child_curlock, ==, homeds::thread::LOCKTYPE_WRITE, child_node);

        // Real time to split the node and get point at which it was split
        ret = split_node(my_node, child_node, child_ind, &split_key, bcp);
        if (ret != btree_status_t::success) { goto out; }

        // After split, retry search and walk down.
        unlock_node(child_node, homeds::thread::LOCKTYPE_WRITE);
        child_curlock = LOCKTYPE_NONE;
        COUNTER_INCREMENT(m_metrics, btree_split_count, 1);
        split_occured = true;
    out:
        if (ret != btree_status_t::success) {
            if (curlock != LOCKTYPE_NONE) {
                unlock_node(my_node, curlock);
                curlock = LOCKTYPE_NONE;
            }

            if (child_curlock != LOCKTYPE_NONE) {
                unlock_node(child_node, child_curlock);
                child_curlock = LOCKTYPE_NONE;
            }
        }
        return ret;
    }

    /* This function is called for the interior nodes whose childs are leaf nodes to calculate the sub range */
    void get_subrange(const BtreeNodePtr< K >& my_node, BtreeUpdateRequest< K, V >* bur, int curr_ind,
                      K& subrange_start_key, K& subrange_end_key, bool& subrange_start_inc, bool& subrange_end_inc) {

#ifndef NDEBUG
        if (curr_ind > 0) {
            /* start of subrange will always be more then the key in curr_ind - 1 */
            K start_key;
            BtreeKey* start_key_ptr = &start_key;

            my_node->get_nth_key(curr_ind - 1, start_key_ptr, false);
            HS_ASSERT_CMP(DEBUG, start_key_ptr->compare(bur->get_input_range().get_start_key()), <=, 0);
        }
#endif

        // find end of subrange
        bool end_inc = true;
        K end_key;
        BtreeKey* end_key_ptr = &end_key;

        if (curr_ind < (int)my_node->get_total_entries()) {
            my_node->get_nth_key(curr_ind, end_key_ptr, false);
            if (end_key_ptr->compare(bur->get_input_range().get_end_key()) >= 0) {
                /* this is last index to process as end of range is smaller then key in this node */
                end_key_ptr = const_cast< BtreeKey* >(bur->get_input_range().get_end_key());
                end_inc = bur->get_input_range().is_end_inclusive();
            } else {
                end_inc = true;
            }
        } else {
            /* it is the edge node. end key is the end of input range */
            BT_LOG_ASSERT_CMP(my_node->has_valid_edge(), ==, true, my_node);
            end_key_ptr = const_cast< BtreeKey* >(bur->get_input_range().get_end_key());
            end_inc = bur->get_input_range().is_end_inclusive();
        }

        BtreeSearchRange& input_range = bur->get_input_range();
        auto start_key_ptr = input_range.get_start_key();
        subrange_start_key.copy_blob(start_key_ptr->get_blob());
        subrange_end_key.copy_blob(end_key_ptr->get_blob());
        subrange_start_inc = input_range.is_start_inclusive();
        subrange_end_inc = end_inc;

        auto ret = subrange_start_key.compare(&subrange_end_key);
        BT_RELEASE_ASSERT_CMP(ret, <=, 0, my_node);
        ret = subrange_start_key.compare(bur->get_input_range().get_end_key());
        BT_RELEASE_ASSERT_CMP(ret, <=, 0, my_node);
        /* We don't neeed to update the start at it is updated when entries are inserted in leaf nodes */
    }

    btree_status_t check_split_root(const BtreeMutateRequest& put_req) {
        int ind;
        K split_key;
        BtreeNodePtr< K > child_node = nullptr;
        btree_status_t ret = btree_status_t::success;

        m_btree_lock.write_lock();
        BtreeNodePtr< K > root;

        ret = read_and_lock_root(m_root_node, root, locktype::LOCKTYPE_WRITE, locktype::LOCKTYPE_WRITE);
        if (ret != btree_status_t::success) { goto done; }

        if (!root->is_split_needed(m_cfg, put_req)) {
            unlock_node(root, homeds::thread::LOCKTYPE_WRITE);
            goto done;
        }

        // Create a new child node and split them
        child_node = alloc_interior_node();
        if (child_node == nullptr) {
            ret = btree_status_t::space_not_avail;
            unlock_node(root, homeds::thread::LOCKTYPE_WRITE);
            goto done;
        }

        /* it swap the data while keeping the nodeid same */
        btree_store_t::swap_node(m_btree_store.get(), root, child_node);
        write_node(child_node);

        THIS_BT_LOG(DEBUG, btree_structures, root,
                    "Root node is full, swapping contents with child_node {} and split that",
                    child_node->get_node_id());

        BT_DEBUG_ASSERT_CMP(root->get_total_entries(), ==, 0, root);
        ret = split_node(root, child_node, root->get_total_entries(), &split_key, true);
        BT_DEBUG_ASSERT_CMP(m_root_node, ==, root->get_node_id(), root);

        if (ret != btree_status_t::success) {
            btree_store_t::swap_node(m_btree_store.get(), child_node, root);
            write_node(child_node);
        }

        /* unlock child node */
        unlock_node(root, homeds::thread::LOCKTYPE_WRITE);

        if (ret == btree_status_t::success) { COUNTER_INCREMENT(m_metrics, btree_depth, 1); }
    done:
        m_btree_lock.unlock();
        return ret;
    }

    btree_status_t check_collapse_root(const btree_cp_ptr& bcp) {
        BtreeNodePtr< K > child_node = nullptr;
        btree_status_t ret = btree_status_t::success;
        std::vector< BtreeNodePtr< K > > old_nodes;
        std::vector< BtreeNodePtr< K > > new_nodes;

        m_btree_lock.write_lock();
        BtreeNodePtr< K > root;

        ret = read_and_lock_root(m_root_node, root, locktype::LOCKTYPE_WRITE, locktype::LOCKTYPE_WRITE, bcp);
        if (ret != btree_status_t::success) { goto done; }

        if (root->get_total_entries() != 0 || root->is_leaf() /*some other thread collapsed root already*/) {
            unlock_node(root, locktype::LOCKTYPE_WRITE);
            goto done;
        }

        BT_DEBUG_ASSERT_CMP(root->has_valid_edge(), ==, true, root);
        ret = read_node(root->get_edge_id(), child_node);
        if (child_node == nullptr) {
            unlock_node(root, locktype::LOCKTYPE_WRITE);
            goto done;
        }

        // Elevate the edge child as root.
        btree_store_t::swap_node(m_btree_store.get(), root, child_node);
        write_node(root, bcp);
        BT_DEBUG_ASSERT_CMP(m_root_node, ==, root->get_node_id(), root);

        old_nodes.push_back(child_node);

        if (BtreeStoreType == btree_store_type::SSD_BTREE) {
            auto j_iob = btree_store_t::make_journal_entry(journal_op::BTREE_MERGE, true /* is_root */, bcp);
            btree_store_t::append_node_to_journal(j_iob, bt_journal_node_op::inplace_write, root, bcp);
            btree_store_t::append_node_to_journal(j_iob, bt_journal_node_op::removal, child_node, bcp);
            btree_store_t::write_journal_entry(m_btree_store.get(), bcp, j_iob);
        }
        unlock_node(root, locktype::LOCKTYPE_WRITE);
        free_node(child_node, (bcp ? bcp->free_blkid_list : nullptr));

        if (ret == btree_status_t::success) { COUNTER_DECREMENT(m_metrics, btree_depth, 1); }
    done:
        m_btree_lock.unlock();
        return ret;
    }

    btree_status_t split_node(const BtreeNodePtr< K >& parent_node, BtreeNodePtr< K > child_node, uint32_t parent_ind,
                              BtreeKey* out_split_key, const btree_cp_ptr& bcp, bool root_split = false) {
        BtreeNodeInfo ninfo;
        BtreeNodePtr< K > child_node1 = child_node;
        BtreeNodePtr< K > child_node2 = child_node1->is_leaf() ? alloc_leaf_node() : alloc_interior_node();

        if (child_node2 == nullptr) { return (btree_status_t::space_not_avail); }

        btree_status_t ret = btree_status_t::success;

        child_node2->set_next_bnode(child_node1->next_bnode());
        child_node1->set_next_bnode(child_node2->get_node_id());
        uint32_t child1_filled_size = m_cfg.get_node_area_size() - child_node1->get_available_size(m_cfg);

        auto split_size = m_cfg.get_split_size(child1_filled_size);
        uint32_t res = child_node1->move_out_to_right_by_size(m_cfg, child_node2, split_size);

        BT_RELEASE_ASSERT_CMP(res, >, 0, child_node1,
                              "Unable to split entries in the child node"); // means cannot split entries
        BT_DEBUG_ASSERT_CMP(child_node1->get_total_entries(), >, 0, child_node1);

        // Update the existing parent node entry to point to second child ptr.
        bool edge_split = (parent_ind == parent_node->get_total_entries());
        ninfo.set_bnode_id(child_node2->get_node_id());
        parent_node->update(parent_ind, ninfo);

        // Insert the last entry in first child to parent node
        child_node1->get_last_key(out_split_key);
        ninfo.set_bnode_id(child_node1->get_node_id());

        /* If key is extent then we always insert the end key in the parent node */
        K out_split_end_key;
        out_split_end_key.copy_end_key_blob(out_split_key->get_blob());
        parent_node->insert(out_split_end_key, ninfo);

#ifndef NDEBUG
        K split_key;
        child_node2->get_first_key(&split_key);
        BT_DEBUG_ASSERT_CMP(split_key.compare(out_split_key), >, 0, child_node2);
#endif
        THIS_BT_LOG(DEBUG, btree_structures, parent_node, "Split child_node={} with new_child_node={}, split_key={}",
                    child_node1->get_node_id(), child_node2->get_node_id(), out_split_key->to_string());

        if (BtreeStoreType == btree_store_type::SSD_BTREE) {
            auto j_iob = btree_store_t::make_journal_entry(journal_op::BTREE_SPLIT, root_split, bcp,
                                                           {parent_node->get_node_id(), parent_node->get_gen()});
            btree_store_t::append_node_to_journal(
                j_iob, (root_split ? bt_journal_node_op::creation : bt_journal_node_op::inplace_write), child_node1,
                bcp, out_split_end_key.get_blob());

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

        // we write right child node, than left and than parent child
        write_node(child_node2, nullptr, bcp);
        write_node(child_node1, child_node2, bcp);
        write_node(parent_node, child_node1, bcp);

        // NOTE: Do not access parentInd after insert, since insert would have
        // shifted parentNode to the right.
        return ret;
    }

    btree_status_t create_btree_replay(btree_journal_entry* jentry, const btree_cp_ptr& bcp) {
        if (jentry) {
            BT_DEBUG_ASSERT_CMP(jentry->is_root, ==, true, ,
                                "Expected create_btree_replay entry to be root journal entry");
            BT_DEBUG_ASSERT_CMP(jentry->parent_node.get_id(), ==, m_root_node, , "Root node journal entry mismatch");
        }

        // Create a root node by reserving the leaf node
        BtreeNodePtr< K > root = reserve_leaf_node(BlkId(m_root_node));
        auto ret = write_node(root, nullptr, bcp);
        BT_DEBUG_ASSERT_CMP(ret, ==, btree_status_t::success, , "expecting success in writing root node");
        return btree_status_t::success;
    }

    btree_status_t split_node_replay(btree_journal_entry* jentry, const btree_cp_ptr& bcp) {
        bnodeid_t id = jentry->is_root ? m_root_node : jentry->parent_node.node_id;
        BtreeNodePtr< K > parent_node;

        // read parent node
        read_node_or_fail(id, parent_node);

        // Parent already went ahead of the journal entry, return done
        if (parent_node->get_gen() >= jentry->parent_node.node_gen) {
            THIS_BT_LOG(INFO, base, ,
                        "Journal replay: parent_node gen {} ahead of jentry gen {} is root {} , skipping ",
                        parent_node->get_gen(), jentry->parent_node.get_gen(), jentry->is_root);
            return btree_status_t::replay_not_needed;
        }

        // Read the first inplace write node which is the leftmost child and also form child split key from journal
        auto j_child_nodes = jentry->get_nodes();

        BtreeNodePtr< K > child_node1;
        if (jentry->is_root) {
            // If root is not written yet, parent_node will be pointing child_node1, so create a new parent_node to
            // be treated as root here on.
            child_node1 = reserve_interior_node(BlkId(j_child_nodes[0]->node_id()));
            btree_store_t::swap_node(m_btree_store.get(), parent_node, child_node1);

            THIS_BT_LOG(INFO, btree_generics, ,
                        "Journal replay: root split, so creating child_node id={} and swapping the node with "
                        "parent_node id={} names {}",
                        child_node1->get_node_id(), parent_node->get_node_id(), m_cfg.get_name());

        } else {
            read_node_or_fail(j_child_nodes[0]->node_id(), child_node1);
        }

        THIS_BT_LOG(INFO, btree_generics, ,
                    "Journal replay: child_node1 => jentry: [id={} gen={}], ondisk: [id={} gen={}] names {}",
                    j_child_nodes[0]->node_id(), j_child_nodes[0]->node_gen(), child_node1->get_node_id(),
                    child_node1->get_gen(), m_cfg.get_name());
        if (jentry->is_root) {
            BT_RELEASE_ASSERT_CMP(j_child_nodes[0]->type, ==, bt_journal_node_op::creation, ,
                                  "Expected first node in journal entry to be new creation for root split");
        } else {
            BT_RELEASE_ASSERT_CMP(j_child_nodes[0]->type, ==, bt_journal_node_op::inplace_write, ,
                                  "Expected first node in journal entry to be in-place write");
        }
        BT_RELEASE_ASSERT_CMP(j_child_nodes[1]->type, ==, bt_journal_node_op::creation, ,
                              "Expected second node in journal entry to be new node creation");

        // recover child node
        bool child_split = recover_child_nodes_in_split(child_node1, j_child_nodes, bcp);

        // recover parent node
        recover_parent_node_in_split(parent_node, child_split ? child_node1 : nullptr, j_child_nodes, bcp);
        return btree_status_t::success;
    }

    bool recover_child_nodes_in_split(const BtreeNodePtr< K >& child_node1,
                                      const std::vector< bt_journal_node_info* >& j_child_nodes,
                                      const btree_cp_ptr& bcp) {

        BtreeNodePtr< K > child_node2;
        // Check if child1 is ahead of the generation
        if (child_node1->get_gen() >= j_child_nodes[0]->node_gen()) {
            // leftmost_node is written, so right node must have been written as well.
            read_node_or_fail(child_node1->next_bnode(), child_node2);

            // sanity check for right node
            BT_RELEASE_ASSERT_CMP(child_node2->get_gen(), >=, j_child_nodes[1]->node_gen(), child_node2,
                                  "gen cnt should be more than the journal entry");
            // no need to recover child nodes
            return false;
        }

        K split_key;
        split_key.set_blob({j_child_nodes[0]->key_area(), j_child_nodes[0]->key_size});
        child_node2 = child_node1->is_leaf() ? reserve_leaf_node(BlkId(j_child_nodes[1]->node_id()))
                                             : reserve_interior_node(BlkId(j_child_nodes[1]->node_id()));

        // We need to do split based on entries since the left children is also not written yet.
        // Find the split key within the child_node1. It is not always found, so we split upto that.
        auto ret = child_node1->find(split_key, nullptr, false);

        // sanity check for left mode node before recovery
        {
            if (!ret.found) {
                if (!child_node1->is_leaf()) {
                    BT_RELEASE_ASSERT(0, , "interior nodes should always have this key if it is written yet");
                }
            }
        }

        THIS_BT_LOG(INFO, btree_generics, , "Journal replay: split key {}, split indx {} child_node1 {}",
                    split_key.to_string(), ret.end_of_search_index, child_node1->to_string());
        /* if it is not found than end_of_search_index points to first ind which is greater than split key */
        auto split_ind = ret.end_of_search_index;
        if (ret.found) { ++split_ind; } // we don't want to move split key */
        if (child_node1->is_leaf() && split_ind < (int)child_node1->get_total_entries()) {
            K key;
            child_node1->get_nth_key(split_ind, &key, false);

            if (split_key.compare_start(&key) >= 0) { /* we need to split the key range */
                THIS_BT_LOG(INFO, btree_generics, , "splitting a leaf node key {}", key.to_string());
                V v;
                child_node1->get_nth_value(split_ind, &v, false);
                vector< pair< K, V > > replace_kv;
                child_node1->remove(split_ind, split_ind);
                m_split_key_cb(key, v, split_key, replace_kv);
                for (auto& pair : replace_kv) {
                    auto status = child_node1->insert(pair.first, pair.second);
                    BT_RELEASE_ASSERT((status == btree_status_t::success), child_node1, "unexpected insert failure");
                }
                auto ret = child_node1->find(split_key, nullptr, false);
                BT_RELEASE_ASSERT((ret.found && (ret.end_of_search_index == split_ind)), child_node1,
                                  "found new indx {}, old split indx{}", ret.end_of_search_index, split_ind);
                ++split_ind;
            }
        }
        child_node1->move_out_to_right_by_entries(m_cfg, child_node2, child_node1->get_total_entries() - split_ind);

        child_node2->set_next_bnode(child_node1->next_bnode());
        child_node2->set_gen(j_child_nodes[1]->node_gen());

        child_node1->set_next_bnode(child_node2->get_node_id());
        child_node1->set_gen(j_child_nodes[0]->node_gen());

        THIS_BT_LOG(INFO, btree_generics, , "Journal replay: child_node2 {}", child_node2->to_string());
        write_node(child_node2, nullptr, bcp);
        write_node(child_node1, child_node2, bcp);
        return true;
    }

    void recover_parent_node_in_split(const BtreeNodePtr< K >& parent_node, const BtreeNodePtr< K >& child_node1,
                                      std::vector< bt_journal_node_info* >& j_child_nodes, const btree_cp_ptr& bcp) {

        // find child_1 key
        K child1_key; // we need to insert child1_key
        BT_RELEASE_ASSERT_CMP(j_child_nodes[0]->key_size, !=, 0, , "key size of left mode node is zero");
        child1_key.set_blob({j_child_nodes[0]->key_area(), j_child_nodes[0]->key_size});
        auto child1_node_id = j_child_nodes[0]->node_id();

        // find split indx
        auto ret = parent_node->find(child1_key, nullptr, false);
        BT_RELEASE_ASSERT_CMP(ret.found, ==, false, , "child_1 key should not be in this parent");
        auto split_indx = ret.end_of_search_index;

        // find child2_key
        K child2_key; // we only need to update child2_key to new node
        if (j_child_nodes[1]->key_size != 0) {
            child2_key.set_blob({j_child_nodes[1]->key_area(), j_child_nodes[1]->key_size});
            ret = parent_node->find(child2_key, nullptr, false);
            BT_RELEASE_ASSERT_CMP(split_indx, ==, ret.end_of_search_index, , "it should be same as split index");
        } else {
            // parent should be valid edge it is not a root split
        }
        auto child2_node_id = j_child_nodes[1]->node_id();

        // update child2_key value
        BtreeNodeInfo ninfo;
        ninfo.set_bnode_id(child2_node_id);
        parent_node->update(split_indx, ninfo);

        // insert child 1
        ninfo.set_bnode_id(child1_node_id);
        K out_split_end_key;
        out_split_end_key.copy_end_key_blob(child1_key.get_blob());
        parent_node->insert(out_split_end_key, ninfo);

        // Write the parent node
        write_node(parent_node, child_node1, bcp);

        /* do sanity check after recovery split */
        {
            validate_sanity_child(parent_node, split_indx);
            validate_sanity_next_child(parent_node, split_indx);
        }
    }

    btree_status_t merge_nodes(const BtreeNodePtr< K >& parent_node, uint32_t start_indx, uint32_t end_indx,
                               const btree_cp_ptr& bcp) {
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
                BT_LOG_ASSERT(parent_node->has_valid_edge(), parent_node,
                              "Assertion failure, expected valid edge for parent_node: {}");
            }

            BtreeNodeInfo child_info;
            parent_node->get(indx, &child_info, false /* copy */);

            BtreeNodePtr< K > child;
            ret = read_and_lock_node(child_info.bnode_id(), child, locktype::LOCKTYPE_WRITE, locktype::LOCKTYPE_WRITE,
                                     bcp);
            if (ret != btree_status_t::success) { goto out; }
            BT_LOG_ASSERT_CMP(child->is_valid_node(), ==, true, child);

            /* check if left most node has space */
            if (indx == start_indx) {
                balanced_size = m_cfg.get_ideal_fill_size();
                left_most_node = child;
                if (left_most_node->get_occupied_size(m_cfg) > balanced_size) {
                    /* first node doesn't have any free space. we can exit now */
                    ret = btree_status_t::merge_not_required;
                    goto out;
                }
            } else {
                bool is_allocated = true;
                /* pre allocate the new nodes. We will free the nodes which are not in use later */
                auto new_node = btree_store_t::alloc_node(m_btree_store.get(), child->is_leaf(), is_allocated, child);
                if (is_allocated) {
                    /* we are going to allocate new blkid of all the nodes except the first node.
                     * Note :- These blkids will leak if we fail or crash before writing entry into
                     * journal.
                     */
                    old_nodes.push_back(child);
                    COUNTER_INCREMENT_IF_ELSE(m_metrics, child->is_leaf(), btree_leaf_node_count, btree_int_node_count,
                                              1);
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
            auto occupied_size = merge_node->get_occupied_size(m_cfg);
            if (occupied_size < balanced_size) {
                uint32_t pull_size = balanced_size - occupied_size;
                merge_node->move_in_from_right_by_size(m_cfg, new_nodes[i], pull_size);
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
            BT_DEBUG_ASSERT_CMP(last_ckey.compare(&last_pkey), <=, 0, parent_node);
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

        /* write the journal entry */
        if (BtreeStoreType == btree_store_type::SSD_BTREE) {
            auto j_iob = btree_store_t::make_journal_entry(journal_op::BTREE_MERGE, false /* is_root */, bcp,
                                                           {parent_node->get_node_id(), parent_node->get_gen()});
            K child_pkey;
            if (start_indx < parent_node->get_total_entries()) {
                parent_node->get_nth_key(start_indx, &child_pkey, true);
                BT_RELEASE_ASSERT_CMP(start_indx, ==, (parent_insert_indx - 1), parent_node, "it should be last index");
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
                    BT_RELEASE_ASSERT_CMP((start_indx + insert_indx), ==, (parent_insert_indx - 1), parent_node,
                                          "it should be last index");
                }
                btree_store_t::append_node_to_journal(j_iob, bt_journal_node_op::creation, node, bcp,
                                                      child_pkey.get_blob());
                ++insert_indx;
            }
            BT_RELEASE_ASSERT_CMP((start_indx + insert_indx), ==, parent_insert_indx, parent_node, "it should be same");
            btree_store_t::write_journal_entry(m_btree_store.get(), bcp, j_iob);
        }

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
        HS_DEBUG_ASSERT_EQ(total_child_entries, new_entries);

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
            unlock_node(child_nodes[i], locktype::LOCKTYPE_WRITE);
        }
        unlock_node(child_nodes[0], locktype::LOCKTYPE_WRITE);
        if (ret != btree_status_t::success) {
            /* free the allocated nodes */
            for (const auto& n : new_nodes) {
                free_node(n);
            }
        }
        return ret;
    }

#if 0
                btree_status_t merge_node_replay(btree_journal_entry* jentry, const btree_cp_ptr& bcp) {
                    BtreeNodePtr< K > parent_node = (jentry->is_root) ? read_node(m_root_node) : read_node(jentry->parent_node.node_id);

                    // Parent already went ahead of the journal entry, return done
                    if (parent_node->get_gen() >= jentry->parent_node.node_gen) { return btree_status_t::replay_not_needed; }
                }
#endif

    void validate_sanity_child(const BtreeNodePtr< K >& parent_node, uint32_t ind) {
        BtreeNodeInfo child_info;
        K child_first_key;
        K child_last_key;
        K parent_key;

        parent_node->get(ind, &child_info, false /* copy */);
        BtreeNodePtr< K > child_node = nullptr;
        auto ret = read_node(child_info.bnode_id(), child_node);
        BT_REL_ASSERT_EQ(ret, btree_status_t::success, "read failed, reason: {}", ret);
        if (child_node->get_total_entries() == 0) {
            auto parent_entries = parent_node->get_total_entries();
            if (!child_node->is_leaf()) { // leaf node or edge node can have 0 entries
                BT_REL_ASSERT_EQ(((parent_node->has_valid_edge() && ind == parent_entries)), true);
            }
            return;
        }
        child_node->get_first_key(&child_first_key);
        child_node->get_last_key(&child_last_key);
        BT_REL_ASSERT_LE(child_first_key.compare(&child_last_key), 0)
        if (ind == parent_node->get_total_entries()) {
            BT_REL_ASSERT_EQ(parent_node->has_valid_edge(), true);
            if (ind > 0) {
                parent_node->get_nth_key(ind - 1, &parent_key, false);
                BT_REL_ASSERT_GT(child_first_key.compare(&parent_key), 0)
                BT_REL_ASSERT_LT(parent_key.compare_start(&child_first_key), 0)
            }
        } else {
            parent_node->get_nth_key(ind, &parent_key, false);
            BT_REL_ASSERT_LE(child_first_key.compare(&parent_key), 0)
            BT_REL_ASSERT_LE(child_last_key.compare(&parent_key), 0)
            BT_REL_ASSERT_GE(parent_key.compare_start(&child_first_key), 0)
            BT_REL_ASSERT_GE(parent_key.compare_start(&child_first_key), 0)
            if (ind != 0) {
                parent_node->get_nth_key(ind - 1, &parent_key, false);
                BT_REL_ASSERT_GT(child_first_key.compare(&parent_key), 0)
                BT_REL_ASSERT_LT(parent_key.compare_start(&child_first_key), 0)
            }
        }
    }

    void validate_sanity_next_child(const BtreeNodePtr< K >& parent_node, uint32_t ind) {
        BtreeNodeInfo child_info;
        K child_key;
        K parent_key;

        if (parent_node->has_valid_edge()) {
            if (ind == parent_node->get_total_entries()) { return; }
        } else {
            if (ind == parent_node->get_total_entries() - 1) { return; }
        }
        parent_node->get(ind + 1, &child_info, false /* copy */);
        BtreeNodePtr< K > child_node = nullptr;
        auto ret = read_node(child_info.bnode_id(), child_node);
        HS_RELEASE_ASSERT(ret == btree_status_t::success, "read failed, reason: {}", ret);
        if (child_node->get_total_entries() == 0) {
            auto parent_entries = parent_node->get_total_entries();
            if (!child_node->is_leaf()) { // leaf node can have 0 entries
                HS_ASSERT_CMP(RELEASE,
                              ((parent_node->has_valid_edge() && ind == parent_entries) || (ind = parent_entries - 1)),
                              ==, true);
            }
            return;
        }
        /* in case of merge next child will never have zero entries otherwise it would have been merged */
        HS_ASSERT_CMP(RELEASE, child_node->get_total_entries(), !=, 0);
        child_node->get_first_key(&child_key);
        parent_node->get_nth_key(ind, &parent_key, false);
        BT_REL_ASSERT_GT(child_key.compare(&parent_key), 0)
        BT_REL_ASSERT_GT(parent_key.compare_start(&child_key), 0)
    }

    void print_node(const bnodeid_t& bnodeid) {
        std::string buf;
        BtreeNodePtr< K > node;

        m_btree_lock.read_lock();
        homeds::thread::locktype acq_lock = homeds::thread::locktype::locktype_t::READ;
        if (read_and_lock_node(bnodeid, node, acq_lock, acq_lock, nullptr) != btree_status_t::success) { goto done; }
        buf = node->to_string(true /* print_friendly */);
        unlock_node(node, acq_lock);

    done:
        m_btree_lock.unlock();

        THIS_BT_LOG(INFO, base, , "Node: <{}>", buf);
    }

    void diff(Btree* other, uint32_t param, vector< pair< K, V > >* diff_kv) {
        std::vector< pair< K, V > > my_kvs, other_kvs;

        get_all_kvs(&my_kvs);
        other->get_all_kvs(&other_kvs);
        auto it1 = my_kvs.begin();
        auto it2 = other_kvs.begin();

        K k1, k2;
        V v1, v2;

        if (it1 != my_kvs.end()) {
            k1 = it1->first;
            v1 = it1->second;
        }
        if (it2 != other_kvs.end()) {
            k2 = it2->first;
            v2 = it2->second;
        }

        while ((it1 != my_kvs.end()) && (it2 != other_kvs.end())) {
            if (k1.preceeds(&k2)) {
                /* k1 preceeds k2 - push k1 and continue */
                diff_kv->emplace_back(make_pair(k1, v1));
                it1++;
                if (it1 == my_kvs.end()) { break; }
                k1 = it1->first;
                v1 = it1->second;
            } else if (k1.succeeds(&k2)) {
                /* k2 preceeds k1 - push k2 and continue */
                diff_kv->emplace_back(make_pair(k2, v2));
                it2++;
                if (it2 == other_kvs.end()) { break; }
                k2 = it2->first;
                v2 = it2->second;
            } else {
                /* k1 and k2 overlaps */
                std::vector< pair< K, V > > overlap_kvs;
                diff_read_next_t to_read = READ_BOTH;

                v1.get_overlap_diff_kvs(&k1, &v1, &k2, &v2, param, to_read, overlap_kvs);
                for (auto ovr_it = overlap_kvs.begin(); ovr_it != overlap_kvs.end(); ovr_it++) {
                    diff_kv->emplace_back(make_pair(ovr_it->first, ovr_it->second));
                }

                switch (to_read) {
                case READ_FIRST:
                    it1++;
                    if (it1 == my_kvs.end()) {
                        // Add k2,v2
                        diff_kv->emplace_back(make_pair(k2, v2));
                        it2++;
                        break;
                    }
                    k1 = it1->first;
                    v1 = it1->second;
                    break;

                case READ_SECOND:
                    it2++;
                    if (it2 == other_kvs.end()) {
                        diff_kv->emplace_back(make_pair(k1, v1));
                        it1++;
                        break;
                    }
                    k2 = it2->first;
                    v2 = it2->second;
                    break;

                case READ_BOTH:
                    /* No tail part */
                    it1++;
                    if (it1 == my_kvs.end()) { break; }
                    k1 = it1->first;
                    v1 = it1->second;
                    it2++;
                    if (it2 == my_kvs.end()) { break; }
                    k2 = it2->first;
                    v2 = it2->second;
                    break;

                default:
                    LOGERROR("ERROR: Getting Overlapping Diff KVS for {}:{}, {}:{}, to_read {}", k1, v1, k2, v2,
                             to_read);
                    /* skip both */
                    it1++;
                    if (it1 == my_kvs.end()) { break; }
                    k1 = it1->first;
                    v1 = it1->second;
                    it2++;
                    if (it2 == my_kvs.end()) { break; }
                    k2 = it2->first;
                    v2 = it2->second;
                    break;
                }
            }
        }

        while (it1 != my_kvs.end()) {
            diff_kv->emplace_back(make_pair(it1->first, it1->second));
            it1++;
        }

        while (it2 != other_kvs.end()) {
            diff_kv->emplace_back(make_pair(it2->first, it2->second));
            it2++;
        }
    }

    void merge(Btree* other, match_item_cb_t< K, V > merge_cb) {
        std::vector< pair< K, V > > other_kvs;

        other->get_all_kvs(&other_kvs);
        for (auto it = other_kvs.begin(); it != other_kvs.end(); it++) {
            K k = it->first;
            V v = it->second;
            BRangeCBParam local_param(k, v);
            K start(k.start(), 1), end(k.end(), 1);

            auto search_range = BtreeSearchRange(start, true, end, true);
            BtreeUpdateRequest< K, V > ureq(search_range, merge_cb, nullptr, (BRangeCBParam*)&local_param);
            range_put(k, v, btree_put_type::APPEND_IF_EXISTS_ELSE_INSERT, nullptr, nullptr, ureq);
        }
    }

    template < btree_store_type BtreeStoreType, typename K, typename V, btree_node_type InteriorNodeType,
               btree_node_type LeafNodeType >
    thread_local homeds::reserve_vector< btree_locked_node_info, 5 > btree_t::wr_locked_nodes;

    template < btree_store_type BtreeStoreType, typename K, typename V, btree_node_type InteriorNodeType,
               btree_node_type LeafNodeType >
    thread_local homeds::reserve_vector< btree_locked_node_info, 5 > btree_t::rd_locked_nodes;
};
} // namespace btree
} // namespace sisl
