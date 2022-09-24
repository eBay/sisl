#pragma once

namespace sisl {
template < typename K, typename V >
class HSBtree : public Btree< K, V > {
    static btree_t* create_btree(const btree_super_block& btree_sb, const BtreeConfig& cfg, btree_cp_sb* cp_sb,
                                 const split_key_callback& split_key_cb) {
        Btree* bt = new Btree(cfg);
        auto impl_ptr = btree_store_t::init_btree(bt, cfg);
        bt->m_btree_store = std::move(impl_ptr);
        bt->init_recovery(btree_sb, cp_sb, split_key_cb);
        LOGINFO("btree recovered and created {}, node size {}", cfg.get_name(), cfg.get_node_size());
        return bt;
    }

    void init(bool recovery) {
        m_total_nodes = m_last_cp_sb.btree_size;
        m_bt_store->update_sb(m_sb, &m_last_cp_sb, is_recovery);
        create_root_node();
    }

    void init_recovery(const btree_super_block& btree_sb, btree_cp_sb* cp_sb, const split_key_callback& split_key_cb) {
        m_sb = btree_sb;
        m_split_key_cb = split_key_cb;
        if (cp_sb) { memcpy(&m_last_cp_sb, cp_sb, sizeof(m_last_cp_sb)); }
        do_common_init(true);
        m_root_node_id = m_sb.root_node;
    }

    /* It is called when its btree consumer has successfully stored the btree superblock */
    void create_done() { btree_store_t::create_done(m_btree_store.get(), m_root_node_id); }
    void destroy_done() { btree_store_t::destroy_done(m_btree_store.get()); }

    void replay_done(const btree_cp_ptr& bcp) {
        m_total_nodes = m_last_cp_sb.btree_size + bcp->btree_size.load();
        BT_LOG(INFO, base, , "total btree nodes {}", m_total_nodes);
    }

    btree_status_t free_btree(const BtreeNodePtr< K >& start_node, blkid_list_ptr free_blkid_list, bool in_mem,
                              uint64_t& free_node_cnt) {
        // TODO - this calls free node on mem_tree and ssd_tree.
        // In ssd_tree we free actual block id, which is not correct behavior
        // we shouldnt really free any blocks on free node, just reclaim any memory
        // occupied by ssd_tree structure in memory. Ideally we should have sepearte
        // api like deleteNode which should be called instead of freeNode
        const auto ret = post_order_traversal(
            start_node, [this, free_blkid_list, in_mem, &free_node_cnt](const BtreeNodePtr< K >& node) {
                free_node(node, free_blkid_list, in_mem);
                ++free_node_cnt;
            });
        return ret;
    }

    /* It attaches the new CP and prepare for cur cp flush */
    btree_cp_ptr attach_prepare_cp(const btree_cp_ptr& cur_bcp, bool is_last_cp, bool blkalloc_checkpoint) {
        return (btree_store_t::attach_prepare_cp(m_btree_store.get(), cur_bcp, is_last_cp, blkalloc_checkpoint));
    }

    void cp_start(const btree_cp_ptr& bcp, cp_comp_callback cb) {
        btree_store_t::cp_start(m_btree_store.get(), bcp, cb);
    }

    std::string get_cp_flush_status(const btree_cp_ptr& bcp) const {
        return (btree_store_t::get_cp_flush_status(m_btree_store.get(), bcp));
    }

    void truncate(const btree_cp_ptr& bcp) { btree_store_t::truncate(m_btree_store.get(), bcp); }

    /* It is called before superblock is persisted for each CP */
    void update_btree_cp_sb(const btree_cp_ptr& bcp, btree_cp_sb& btree_sb, bool is_blkalloc_cp) {
        btree_sb.active_seqid = bcp->end_seqid;
        btree_sb.blkalloc_cp_id = is_blkalloc_cp ? bcp->cp_id : m_last_cp_sb.blkalloc_cp_id;
        btree_sb.btree_size = bcp->btree_size.load() + m_last_cp_sb.btree_size;
        btree_sb.cp_id = bcp->cp_id;
        HS_DEBUG_ASSERT_EQ((int64_t)m_last_cp_sb.cp_id, (int64_t)bcp->cp_id - 1);
        memcpy(&m_last_cp_sb, &btree_sb, sizeof(m_last_cp_sb));
    }

    void flush_free_blks(const btree_cp_ptr& bcp, std::shared_ptr< homestore::blkalloc_cp >& ba_cp) {
        btree_store_t::flush_free_blks(m_btree_store.get(), bcp, ba_cp);
    }

    /**
     * @brief : verify the btree node is corrupted or not;
     *
     * Note: this function should never assert, but only return success or failure since it is in verification mode;
     *
     * @param bnodeid : node id
     * @param parent_node : parent node ptr
     * @param indx : index within thie node;
     * @param update_debug_bm : true or false;
     *
     * @return : true if this node including all its children are not corrupted;
     *           false if not;
     */
    template < typename K, typename V >
    bool Btree< K, V >::verify_node(bnodeid_t bnodeid, BtreeNodePtr< K > parent_node, uint32_t indx,
                                    bool update_debug_bm) {
        locktype_t acq_lock = locktype_t::READ;
        BtreeNodePtr< K > my_node;
        if (read_and_lock_node(bnodeid, my_node, acq_lock, acq_lock, nullptr) != btree_status_t::success) {
            LOGINFO("read node failed");
            return false;
        }
        if (update_debug_bm &&
            (btree_store_t::update_debug_bm(m_btree_store.get(), my_node) != btree_status_t::success)) {
            LOGERROR("bitmap update failed for node {}", my_node->to_string());
            return false;
        }

        K prev_key;
        bool success = true;
        for (uint32_t i = 0; i < my_node->get_total_entries(); ++i) {
            K key;
            my_node->get_nth_key(i, &key, false);
            if (!my_node->is_leaf()) {
                BtreeNodeInfo child;
                my_node->get(i, &child, false);
                success = verify_node(child.bnode_id(), my_node, i, update_debug_bm);
                if (!success) { goto exit_on_error; }

                if (i > 0) {
                    BT_LOG_ASSERT_CMP(prev_key.compare(&key), <, 0, my_node);
                    if (prev_key.compare(&key) >= 0) {
                        success = false;
                        goto exit_on_error;
                    }
                }
            }
            if (my_node->is_leaf() && i > 0) {
                BT_LOG_ASSERT_CMP(prev_key.compare_start(&key), <, 0, my_node);
                if (prev_key.compare_start(&key) >= 0) {
                    success = false;
                    goto exit_on_error;
                }
            }
            prev_key = key;
        }

        if (my_node->is_leaf() && my_node->get_total_entries() == 0) {
            /* this node has zero entries */
            goto exit_on_error;
        }
        if (parent_node && parent_node->get_total_entries() != indx) {
            K parent_key;
            parent_node->get_nth_key(indx, &parent_key, false);

            K last_key;
            my_node->get_nth_key(my_node->get_total_entries() - 1, &last_key, false);
            if (!my_node->is_leaf()) {
                BT_LOG_ASSERT_CMP(last_key.compare(&parent_key), ==, 0, parent_node,
                                  "last key {} parent_key {} child {}", last_key.to_string(), parent_key.to_string(),
                                  my_node->to_string());
                if (last_key.compare(&parent_key) != 0) {
                    success = false;
                    goto exit_on_error;
                }
            } else {
                BT_LOG_ASSERT_CMP(last_key.compare(&parent_key), <=, 0, parent_node,
                                  "last key {} parent_key {} child {}", last_key.to_string(), parent_key.to_string(),
                                  my_node->to_string());
                if (last_key.compare(&parent_key) > 0) {
                    success = false;
                    goto exit_on_error;
                }
                BT_LOG_ASSERT_CMP(parent_key.compare_start(&last_key), >=, 0, parent_node,
                                  "last key {} parent_key {} child {}", last_key.to_string(), parent_key.to_string(),
                                  my_node->to_string());
                if (parent_key.compare_start(&last_key) < 0) {
                    success = false;
                    goto exit_on_error;
                }
            }
        }

        if (parent_node && indx != 0) {
            K parent_key;
            parent_node->get_nth_key(indx - 1, &parent_key, false);

            K first_key;
            my_node->get_nth_key(0, &first_key, false);
            BT_LOG_ASSERT_CMP(first_key.compare(&parent_key), >, 0, parent_node, "my node {}", my_node->to_string());
            if (first_key.compare(&parent_key) <= 0) {
                success = false;
                goto exit_on_error;
            }

            BT_LOG_ASSERT_CMP(parent_key.compare_start(&first_key), <, 0, parent_node, "my node {}",
                              my_node->to_string());
            if (parent_key.compare_start(&first_key) > 0) {
                success = false;
                goto exit_on_error;
            }
        }

        if (my_node->has_valid_edge()) {
            success = verify_node(my_node->get_edge_id(), my_node, my_node->get_total_entries(), update_debug_bm);
            if (!success) { goto exit_on_error; }
        }

    exit_on_error:
        unlock_node(my_node, acq_lock);
        return success;
    }

    btree_status_t create_btree_replay(btree_journal_entry* jentry, const btree_cp_ptr& bcp) {
        if (jentry) {
            BT_DBG_ASSERT_CMP(jentry->is_root, ==, true, ,
                              "Expected create_btree_replay entry to be root journal entry");
            BT_DBG_ASSERT_CMP(jentry->parent_node.get_id(), ==, m_root_node_id, , "Root node journal entry mismatch");
        }

        // Create a root node by reserving the leaf node
        BtreeNodePtr< K > root = reserve_leaf_node(BlkId(m_root_node_id));
        auto ret = write_node(root, nullptr, bcp);
        BT_DBG_ASSERT_CMP(ret, ==, btree_status_t::success, , "expecting success in writing root node");
        return btree_status_t::success;
    }

    btree_status_t split_node_replay(btree_journal_entry* jentry, const btree_cp_ptr& bcp) {
        bnodeid_t id = jentry->is_root ? m_root_node_id : jentry->parent_node.node_id;
        BtreeNodePtr< K > parent_node;

        // read parent node
        read_node_or_fail(id, parent_node);

        // Parent already went ahead of the journal entry, return done
        if (parent_node->get_gen() >= jentry->parent_node.node_gen) {
            BT_LOG(INFO, base, , "Journal replay: parent_node gen {} ahead of jentry gen {} is root {} , skipping ",
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

            BT_LOG(INFO, btree_generics, ,
                   "Journal replay: root split, so creating child_node id={} and swapping the node with "
                   "parent_node id={} names {}",
                   child_node1->get_node_id(), parent_node->get_node_id(), m_cfg.name());

        } else {
            read_node_or_fail(j_child_nodes[0]->node_id(), child_node1);
        }

        THIS_BT_LOG(INFO, btree_generics, ,
                    "Journal replay: child_node1 => jentry: [id={} gen={}], ondisk: [id={} gen={}] names {}",
                    j_child_nodes[0]->node_id(), j_child_nodes[0]->node_gen(), child_node1->get_node_id(),
                    child_node1->get_gen(), m_cfg.name());
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
};
} // namespace sisl
