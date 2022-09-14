#pragma once
#include "btree.hpp"

namespace sisl {
namespace btree {

template < typename K, typename V >
btree_status_t Btree< K, V >::do_sweep_query(BtreeNodePtr< K >& my_node, BtreeQueryRequest& qreq,
                                             std::vector< std::pair< K, V > >& out_values) const {
    btree_status_t ret = btree_status_t::success;
    if (my_node->is_leaf()) {
        BT_NODE_DBG_ASSERT_GT(qreq.batch_size(), 0, my_node);

        auto count = 0U;
        BtreeNodePtr< K > next_node = nullptr;

        do {
            if (next_node) {
                unlock_node(my_node, locktype_t::READ);
                my_node = next_node;
            }

            BT_NODE_LOG(TRACE, my_node, "Query leaf node");

            uint32_t start_ind = 0u, end_ind = 0u;
            static thread_local std::vector< std::pair< K, V > > s_match_kvs;

            s_match_kvs.clear();
            auto cur_count =
                my_node->get_all(qreq.next_range(), qreq.batch_size() - count, start_ind, end_ind, &s_match_kvs);
            if (cur_count == 0) {
                if (my_node->get_last_key().compare(qreq.input_range().end_key()) >= 0) {
                    // we've covered all lba range, we are done now;
                    break;
                }
            } else {
                // fall through to visit siblings if we haven't covered lba range yet;
                if (m_bt_cfg.is_custom_kv()) {
                    static thread_local std::vector< std::pair< K, V > > s_result_kvs;
                    s_result_kvs.clear();
                    custom_kv_select_for_read(my_node->get_version(), s_match_kvs, s_result_kvs, qreq.next_range(),
                                              qreq);

                    auto ele_to_add = std::min((uint32_t)s_result_kvs.size(), qreq.batch_size());
                    if (ele_to_add > 0) {
                        out_values.insert(out_values.end(), s_result_kvs.begin(), s_result_kvs.begin() + ele_to_add);
                    }
                    count += ele_to_add;
                    BT_NODE_DBG_ASSERT_LE(count, qreq.batch_size(), my_node);
                } else {
                    out_values.insert(std::end(out_values), std::begin(s_match_kvs), std::end(s_match_kvs));
                    count += cur_count;
                }
            }

            // if cur_count is 0, keep querying sibling nodes;
            if (ret == btree_status_t::success && (count < qreq.batch_size())) {
                if (my_node->next_bnode() == empty_bnodeid) { break; }
                ret = read_and_lock_sibling(my_node->next_bnode(), next_node, locktype_t::READ, locktype_t::READ,
                                            nullptr);
                if (ret == btree_status_t::fast_path_not_possible) { break; }

                if (ret != btree_status_t::success) {
                    LOGERROR("read failed btree name {}", m_bt_cfg.name());
                    break;
                }
            } else {
                if (count >= qreq.batch_size()) { ret = btree_status_t::has_more; }
                break;
            }
        } while (true);

        unlock_node(my_node, locktype_t::READ);
        return ret;
    }

    BtreeNodeInfo start_child_info;
    const auto [isfound, idx] = my_node->find(qreq.next_key(), &start_child_info, false);
    ASSERT_IS_VALID_INTERIOR_CHILD_INDX(isfound, idx, my_node);

    BtreeNodePtr< K > child_node;
    ret = read_and_lock_child(start_child_info.bnode_id(), child_node, my_node, idx, locktype_t::READ, locktype_t::READ,
                              nullptr);
    unlock_node(my_node, locktype_t::READ);
    if (ret != btree_status_t::success) { return ret; }
    return (do_sweep_query(child_node, qreq, out_values));
}

template < typename K, typename V >
btree_status_t Btree< K, V >::do_traversal_query(const BtreeNodePtr< K >& my_node, BtreeQueryRequest& qreq,
                                                 std::vector< std::pair< K, V > >& out_values) const {
    btree_status_t ret = btree_status_t::success;
    uint32_t idx;

    if (my_node->is_leaf()) {
        BT_NODE_LOG_ASSERT_GT(qreq.batch_size(), 0, my_node);

        uint32_t start_ind = 0, end_ind = 0;

        static thread_local std::vector< std::pair< K, V > > s_match_kvs;
        s_match_kvs.clear();
        auto cur_count = my_node->get_all(qreq.next_range(), qreq.batch_size() - (uint32_t)out_values.size(), start_ind,
                                          end_ind, &s_match_kvs);

        if (cur_count && m_bt_cfg.is_custom_kv()) {
            static thread_local std::vector< std::pair< K, V > > s_result_kvs;
            s_result_kvs.clear();
            custom_kv_select_for_read(my_node->get_version(), s_match_kvs, s_result_kvs, qreq.next_range(), qreq);

            auto ele_to_add = s_result_kvs.size();
            if (ele_to_add > 0) {
                out_values.insert(out_values.end(), s_result_kvs.begin(), s_result_kvs.begin() + ele_to_add);
            }
        }
        out_values.insert(std::end(out_values), std::begin(s_match_kvs), std::end(s_match_kvs));

        unlock_node(my_node, locktype_t::READ);
        if (ret != btree_status_t::success || out_values.size() >= qreq.batch_size()) {
            if (out_values.size() >= qreq.batch_size()) { ret = btree_status_t::has_more; }
        }

        return ret;
    }

    const auto [start_isfound, start_idx] = my_node->find(qreq.next_key(), nullptr, false);
    auto [end_is_found, end_idx] = my_node->find(qreq.input_range().end_key(), nullptr, false);
    bool unlocked_already = false;

    if (start_idx == my_node->get_total_entries() && !(my_node->has_valid_edge())) {
        goto done; // no results found
    } else if (end_idx == my_node->get_total_entries() && !(my_node->has_valid_edge())) {
        --end_idx; // end is not valid
    }

    BT_NODE_LOG_ASSERT_LE(start_idx, end_idx, my_node);
    idx = start_idx;

    while (idx <= end_idx) {
        BtreeNodeInfo child_info;
        my_node->get_nth_value(idx, &child_info, false);
        BtreeNodePtr< K > child_node = nullptr;
        locktype_t child_cur_lock = locktype_t::READ;
        ret = read_and_lock_child(child_info.bnode_id(), child_node, my_node, idx, child_cur_lock, child_cur_lock,
                                  nullptr);
        if (ret != btree_status_t::success) { break; }

        if (idx == end_idx) {
            // If we have reached the last index, unlock before traversing down, because we no longer need
            // this lock. Holding this lock will impact performance unncessarily.
            unlock_node(my_node, locktype_t::READ);
            unlocked_already = true;
        }
        // TODO - pass sub range if child is leaf
        ret = do_traversal_query(child_node, qreq, out_values);
        if (ret == btree_status_t::has_more) { break; }
        ++idx;
    }
done:
    if (!unlocked_already) { unlock_node(my_node, locktype_t::READ); }

    return ret;
}

template < typename K, typename V >
btree_status_t
Btree< K, V >::custom_kv_select_for_read(uint8_t node_version, const std::vector< std::pair< K, V > >& match_kv,
                                         std::vector< std::pair< K, V > >& replace_kv, const BtreeKeyRange& range,
                                         const BtreeRangeRequest& qreq) const {

    replace_kv = match_kv;
    return btree_status_t::success;
}

#ifdef SERIALIZABLE_QUERY_IMPLEMENTATION
btree_status_t do_serialzable_query(const BtreeNodePtr< K >& my_node, BtreeSerializableQueryRequest& qreq,
                                    std::vector< std::pair< K, V > >& out_values) {

    btree_status_t ret = btree_status_t::success;
    if (my_node->is_leaf) {
        auto count = 0;
        auto start_result = my_node->find(qreq.get_start_of_range(), nullptr, nullptr);
        auto start_ind = start_result.end_of_search_index;

        auto end_result = my_node->find(qreq.get_end_of_range(), nullptr, nullptr);
        auto end_ind = end_result.end_of_search_index;
        if (!end_result.found) { end_ind--; } // not found entries will point to 1 ind after last in range.

        ind = start_ind;
        while ((ind <= end_ind) && (count < qreq.batch_size())) {
            K key;
            V value;
            my_node->get_nth_element(ind, &key, &value, false);

            if (!qreq.m_match_item_cb || qreq.m_match_item_cb(key, value)) {
                out_values.emplace_back(std::make_pair< K, V >(key, value));
                count++;
            }
            ind++;
        }

        bool has_more = ((ind >= start_ind) && (ind < end_ind));
        if (!has_more) {
            unlock_node(my_node, locktype_t::READ);
            get_tracker(qreq)->pop();
            return success;
        }

        return has_more;
    }

    BtreeNodeId start_child_ptr, end_child_ptr;
    auto start_ret = my_node->find(qreq.get_start_of_range(), nullptr, &start_child_ptr);
    ASSERT_IS_VALID_INTERIOR_CHILD_INDX(start_ret, my_node);
    auto end_ret = my_node->find(qreq.get_end_of_range(), nullptr, &end_child_ptr);
    ASSERT_IS_VALID_INTERIOR_CHILD_INDX(end_ret, my_node);

    BtreeNodePtr< K > child_node;
    if (start_ret.end_of_search_index == end_ret.end_of_search_index) {
        BT_LOG_ASSERT_CMP(start_child_ptr, ==, end_child_ptr, my_node);

        ret =
            read_and_lock_node(start_child_ptr.get_node_id(), child_node, locktype_t::READ, locktype_t::READ, nullptr);
        if (ret != btree_status_t::success) {
            unlock_node(my_node, locktype_t::READ);
            return ret;
        }
        unlock_node(my_node, locktype_t::READ);

        // Pop the last node and push this child node
        get_tracker(qreq)->pop();
        get_tracker(qreq)->push(child_node);
        return do_serialzable_query(child_node, qreq, search_range, out_values);
    } else {
        // This is where the deviation of tree happens. Do not pop the node out of lock tracker
        bool has_more = false;

        for (auto i = start_ret.end_of_search_index; i <= end_ret.end_of_search_index; i++) {
            BtreeNodeId child_ptr;
            my_node->get_nth_value(i, &child_ptr, false);
            ret = read_and_lock_node(child_ptr.get_node_id(), child_node, locktype_t::READ, locktype_t::READ, nullptr);
            if (ret != btree_status_t::success) {
                unlock_node(my_node, locktype_t::READ);
                return ret;
            }

            get_tracker(qreq)->push(child_node);

            ret = do_serialzable_query(child_node, qreq, out_values);
            if (ret == BTREE_AGAIN) {
                BT_LOG_ASSERT_CMP(out_values.size(), ==, qreq.batch_size(), );
                break;
            }
        }

        if (ret == BTREE_SUCCESS) {
            unlock_node(my_node, locktype_t::READ);
            HS_DEBUG_ASSERT_EQ(get_tracker(qreq)->top(), my_node);
            get_tracker(qreq)->pop();
        }
        return ret;
    }
}
#endif

#ifdef SERIALIZABLE_QUERY_IMPLEMENTATION
btree_status_t sweep_query(BtreeQueryRequest& qreq, std::vector< std::pair< K, V > >& out_values) {
    COUNTER_INCREMENT(m_metrics, btree_read_ops_count, 1);
    qreq.init_batch_range();

    m_btree_lock.lock_shared();

    BtreeNodePtr< K > root;
    btree_status_t ret = btree_status_t::success;

    ret = read_and_lock_root(m_root_node_id, root, locktype_t::READ, locktype_t::READ, nullptr);
    if (ret != btree_status_t::success) { goto out; }

    ret = do_sweep_query(root, qreq, out_values);
out:
    m_btree_lock.unlock_shared();

#ifndef NDEBUG
    check_lock_debug();
#endif
    return ret;
}

btree_status_t serializable_query(BtreeSerializableQueryRequest& qreq, std::vector< std::pair< K, V > >& out_values) {
    qreq.init_batch_range();

    m_btree_lock.lock_shared();
    BtreeNodePtr< K > node;
    btree_status_t ret;

    if (qreq.is_empty_cursor()) {
        // Initialize a new lock tracker and put inside the cursor.
        qreq.cursor().m_locked_nodes = std::make_unique< BtreeLockTrackerImpl >(this);

        BtreeNodePtr< K > root;
        ret = read_and_lock_root(m_root_node_id, root, locktype_t::READ, locktype_t::READ, nullptr);
        if (ret != btree_status_t::success) { goto out; }
        get_tracker(qreq)->push(root); // Start tracking the locked nodes.
    } else {
        node = get_tracker(qreq)->top();
    }

    ret = do_serialzable_query(node, qreq, out_values);
out:
    m_btree_lock.unlock_shared();

    // TODO: Assert if key returned from do_get is same as key requested, incase of perfect match

#ifndef NDEBUG
    check_lock_debug();
#endif

    return ret;
}

BtreeLockTrackerImpl* get_tracker(BtreeSerializableQueryRequest& qreq) {
    return (BtreeLockTrackerImpl*)qreq->get_cursor.m_locked_nodes.get();
}

template < typename K, typename V >
class BtreeLockTrackerImpl : public BtreeLockTracker {
public:
    BtreeLockTrackerImpl(btree_t* bt) : m_bt(bt) {}

    virtual ~BtreeLockTrackerImpl() {
        while (m_nodes.size()) {
            auto& p = m_nodes.top();
            m_bt->unlock_node(p.first, p.second);
            m_nodes.pop();
        }
    }

    void push(const BtreeNodePtr< K >& node, locktype_t locktype) { m_nodes.emplace(std::make_pair<>(node, locktype)); }

    std::pair< BtreeNodePtr< K >, locktype_t > pop() {
        HS_ASSERT_CMP(DEBUG, m_nodes.size(), !=, 0);
        std::pair< BtreeNodePtr< K >, locktype_t > p;
        if (m_nodes.size()) {
            p = m_nodes.top();
            m_nodes.pop();
        } else {
            p = std::make_pair<>(nullptr, locktype_t::LOCKTYPE_NONE);
        }

        return p;
    }

    BtreeNodePtr< K > top() { return (m_nodes.size == 0) ? nullptr : m_nodes.top().first; }

private:
    btree_t m_bt;
    std::stack< std::pair< BtreeNodePtr< K >, locktype_t > > m_nodes;
};
#endif
} // namespace btree
} // namespace sisl
