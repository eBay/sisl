#pragma once
#include "btree.ipp"

namespace sisl {
namespace btree {
#ifdef INCASE_WE_NEED_COMMON
// Common class for all membtree's
template < typename K, typename V >
class MemBtreeCommon : public BtreeCommon< K, V > {
public:
    void deref_node(BtreeNode< K >* node) override {
        if (node->m_refcount.decrement_testz()) {
            delete node->m_node_buf;
            delete node;
        }
    }
};

MemBtree(BtreeConfig& cfg) : Btree(update_node_area_size(cfg)) {
    Btree< K, V >::create_store_common(btree_store_type::MEM, []() { return std::make_shared< MemBtreeCommon >(); });
}
#endif

template < typename K, typename V >
class MemBtree : public Btree< K, V > {
public:
    MemBtree(const BtreeConfig& cfg) : Btree< K, V >(cfg) {
        BT_LOG(INFO, "New {} being created: Node size {}", btree_store_type(), cfg.node_size());
    }

    virtual ~MemBtree() {
        const auto [ret, free_node_cnt] = this->destroy_btree(nullptr);
        BT_LOG_ASSERT_EQ(ret, btree_status_t::success, "btree destroy failed");
    }

    std::string btree_store_type() const override { return "MEM_BTREE"; }

private:
    BtreeNodePtr< K > alloc_node(bool is_leaf, bool& is_new_allocation, /* is alloced same as copy_from */
                                 const BtreeNodePtr< K >& copy_from = nullptr) override {
        if (copy_from != nullptr) {
            is_new_allocation = false;
            return copy_from;
        }

        is_new_allocation = true;
        uint8_t* node_buf = new uint8_t[this->m_bt_cfg.node_size()];
        auto new_node = this->init_node(node_buf, bnodeid_t{0}, true, is_leaf);
        new_node->set_node_id(bnodeid_t{r_cast< std::uintptr_t >(new_node)});
        new_node->m_refcount.increment();
        return BtreeNodePtr< K >{new_node};
    }

    btree_status_t read_node(bnodeid_t id, BtreeNodePtr< K >& bnode) const override {
        bnode = BtreeNodePtr< K >{r_cast< BtreeNode< K >* >(id)};
        return btree_status_t::success;
    }

    void swap_node(const BtreeNodePtr< K >& node1, const BtreeNodePtr< K >& node2, void* context) override {
        std::swap(node1->m_phys_node_buf, node2->m_phys_node_buf);
    }

    btree_status_t refresh_node(const BtreeNodePtr< K >& bn, bool is_write_modifiable, void* context) const override {
        return btree_status_t::success;
    }

    void free_node(const BtreeNodePtr< K >& node, void* context) override { this->do_free_node(node); }

    void create_tree_precommit(const BtreeNodePtr< K >& root_node, void* op_context) override {}
    void split_node_precommit(const BtreeNodePtr< K >& parent_node, const BtreeNodePtr< K >& child_node1,
                              const BtreeNodePtr< K >& child_node2, bool root_split, bool edge_split,
                              void* context) override {}

    void merge_node_precommit(bool is_root_merge, const BtreeNodePtr< K >& parent_node, uint32_t parent_merge_start_idx,
                              const BtreeNodePtr< K >& child_node1,
                              const std::vector< BtreeNodePtr< K > >* old_child_nodes,
                              const std::vector< BtreeNodePtr< K > >* replace_child_nodes, void* op_context) override {}
#if 0
    static void ref_node(MemBtreeNode* bn) {
        auto mbh = (mem_btree_node_header*)bn;
        LOGMSG_ASSERT_EQ(mbh->magic, 0xDEADBEEF, "Invalid Magic for Membtree node {}, Metrics {}", bn->to_string(),
                         sisl::MetricsFarm::getInstance().get_result_in_json_string());
        mbh->refcount.increment();
    }

    static void deref_node(MemBtreeNode* bn) {
        auto mbh = (mem_btree_node_header*)bn;
        LOGMSG_ASSERT_EQ(mbh->magic, 0xDEADBEEF, "Invalid Magic for Membtree node {}, Metrics {}", bn->to_string(),
                         sisl::MetricsFarm::getInstance().get_result_in_json_string());
        if (mbh->refcount.decrement_testz()) {
            mbh->magic = 0;
            bn->~MemBtreeNode();
            deallocate_mem((uint8_t*)bn);
        }
    }
#endif
};

} // namespace btree
} // namespace sisl
