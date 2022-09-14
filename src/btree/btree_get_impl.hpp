#pragma once
#include "btree.hpp"

namespace sisl {
namespace btree {
template < typename K, typename V >
btree_status_t Btree< K, V >::do_get(const BtreeNodePtr< K >& my_node, BtreeGetRequest& greq) const {
    btree_status_t ret = btree_status_t::success;
    bool is_child_lock = false;
    locktype_t child_locktype;

    if (my_node->is_leaf()) {
        if (is_get_any_request(greq)) {
            auto& gareq = to_get_any_req(greq);
            const auto [found, idx] =
                my_node->get_any(gareq.m_range, gareq.m_outkey.get(), gareq.m_outval.get(), true, true);
            ret = found ? btree_status_t::success : btree_status_t::not_found;
        } else {
            auto& sgreq = to_single_get_req(greq);
            const auto [found, idx] = my_node->find(sgreq.key(), sgreq.m_outval.get(), true);
            ret = found ? btree_status_t::success : btree_status_t::not_found;
        }
        unlock_node(my_node, locktype_t::READ);
        return ret;
    }

    BtreeNodeInfo child_info;
    bool found;
    uint32_t idx;
    if (is_get_any_request(greq)) {
        std::tie(found, idx) = my_node->find(to_get_any_req(greq).m_range.start_key(), &child_info, true);
    } else {
        std::tie(found, idx) = my_node->find(to_single_get_req(greq).key(), &child_info, true);
    }

    ASSERT_IS_VALID_INTERIOR_CHILD_INDX(found, idx, my_node);
    BtreeNodePtr< K > child_node;
    child_locktype = locktype_t::READ;
    ret = read_and_lock_child(child_info.bnode_id(), child_node, my_node, idx, child_locktype, child_locktype, nullptr);
    if (ret != btree_status_t::success) { goto out; }

    unlock_node(my_node, locktype_t::READ);
    return (do_get(child_node, greq));

out:
    unlock_node(my_node, locktype_t::READ);
    return ret;
}
} // namespace btree
} // namespace sisl
