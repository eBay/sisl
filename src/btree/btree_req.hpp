#pragma once
#include "btree_kv.hpp"
#include "fds/buffer.hpp"

namespace sisl {
namespace btree {
// Base class for any btree operations
struct BtreeRequest {
    BtreeRequest() = default;
    BtreeRequest(void* app_ctx, void* op_ctx) : m_app_context{app_ctx}, m_op_context{op_ctx} {}
    void* m_app_context{nullptr};
    void* m_op_context{nullptr};
};

// Base class for all range related operations
struct BtreeRangeRequest : public BtreeRequest {
public:
    const BtreeKeyRange& input_range() const { return m_search_state.input_range(); }
    uint32_t batch_size() const { return m_batch_size; }
    void set_batch_size(uint32_t count) { m_batch_size = count; }

    bool is_empty_cursor() const {
        return ((m_search_state.const_cursor()->m_last_key == nullptr) &&
                (m_search_state.const_cursor()->m_locked_nodes == nullptr));
    }

    BtreeSearchState& search_state() { return m_search_state; }
    BtreeQueryCursor* cursor() { return m_search_state.cursor(); }
    const BtreeQueryCursor* const_cursor() const { return m_search_state.const_cursor(); }
    BtreeKeyRange next_range() const { return m_search_state.next_range(); }

    const BtreeKeyRange& current_sub_range() const { return m_search_state.current_sub_range(); }
    void set_current_sub_range(const BtreeKeyRange& new_sub_range) {
        m_search_state.set_current_sub_range(new_sub_range);
    }
    const BtreeKey& next_key() const { return m_search_state.next_key(); }

protected:
    BtreeRangeRequest(BtreeSearchState&& search_state, void* app_context = nullptr, uint32_t batch_size = UINT32_MAX) :
            BtreeRequest{app_context, nullptr}, m_search_state(std::move(search_state)), m_batch_size(UINT32_MAX) {}

private:
    BtreeSearchState m_search_state;
    uint32_t m_batch_size{1};
};

/////////////////////////// 1: Put Operations /////////////////////////////////////
struct BtreeSinglePutRequest : public BtreeRequest {
public:
    BtreeSinglePutRequest(std::unique_ptr< const BtreeKey > k, std::unique_ptr< const BtreeValue > v,
                          btree_put_type put_type, std::unique_ptr< BtreeValue > existing_val = nullptr) :
            m_k{std::move(k)}, m_v{std::move(v)}, m_put_type{put_type}, m_existing_val{std::move(existing_val)} {}

    const BtreeKey& key() const { return *m_k; }
    const BtreeValue& value() const { return *m_v; }

    std::unique_ptr< const BtreeKey > m_k;
    std::unique_ptr< const BtreeValue > m_v;
    btree_put_type m_put_type;
    std::unique_ptr< BtreeValue > m_existing_val;
};

struct BtreeRangeUpdateRequest : public BtreeRangeRequest {
public:
    BtreeRangeUpdateRequest(BtreeSearchState&& search_state, btree_put_type put_type, const BtreeValue& value,
                            void* app_context = nullptr, uint32_t batch_size = std::numeric_limits< uint32_t >::max()) :
            BtreeRangeRequest(std::move(search_state), app_context, batch_size),
            m_put_type{put_type},
            m_newval{value} {}

    const btree_put_type m_put_type{btree_put_type::INSERT_ONLY_IF_NOT_EXISTS};
    const BtreeValue& m_newval;
};

using BtreeMutateRequest = std::variant< BtreeSinglePutRequest, BtreeRangeUpdateRequest >;

static bool is_range_update_req(BtreeMutateRequest& req) {
    return (std::holds_alternative< BtreeRangeUpdateRequest >(req));
}

static BtreeRangeUpdateRequest& to_range_update_req(BtreeMutateRequest& req) {
    return std::get< BtreeRangeUpdateRequest >(req);
}

static BtreeSinglePutRequest& to_single_put_req(BtreeMutateRequest& req) {
    return std::get< BtreeSinglePutRequest >(req);
}

static void* put_req_op_ctx(BtreeMutateRequest& req) {
    return (is_range_update_req(req)) ? to_range_update_req(req).m_op_context : to_single_put_req(req).m_op_context;
}

/////////////////////////// 2: Remove Operations /////////////////////////////////////
struct BtreeSingleRemoveRequest : public BtreeRequest {
public:
    BtreeSingleRemoveRequest(std::unique_ptr< const BtreeKey > k, std::unique_ptr< BtreeValue > out_val) :
            m_k{std::move(k)}, m_outval{std::move(out_val)} {}

    const BtreeKey& key() const { return *m_k; }
    const BtreeValue& value() const { return *m_outval; }

    std::unique_ptr< const BtreeKey > m_k;
    std::unique_ptr< BtreeValue > m_outval;
};

struct BtreeRemoveAnyRequest : public BtreeRequest {
public:
    BtreeRemoveAnyRequest(BtreeKeyRange&& range, std::unique_ptr< BtreeKey > out_key,
                          std::unique_ptr< BtreeValue > out_val) :
            m_range{std::move(range)}, m_outkey{std::move(out_key)}, m_outval{std::move(out_val)} {}

    BtreeKeyRange m_range;
    std::unique_ptr< BtreeKey > m_outkey;
    std::unique_ptr< BtreeValue > m_outval;
};

using BtreeRemoveRequest = std::variant< BtreeSingleRemoveRequest, BtreeRemoveAnyRequest >;

static bool is_remove_any_request(BtreeRemoveRequest& req) {
    return (std::holds_alternative< BtreeRemoveAnyRequest >(req));
}

static BtreeSingleRemoveRequest& to_single_remove_req(BtreeRemoveRequest& req) {
    return std::get< BtreeSingleRemoveRequest >(req);
}

static BtreeRemoveAnyRequest& to_remove_any_req(BtreeRemoveRequest& req) {
    return std::get< BtreeRemoveAnyRequest >(req);
}

static void* remove_req_op_ctx(BtreeRemoveRequest& req) {
    return (is_remove_any_request(req)) ? to_remove_any_req(req).m_op_context : to_single_remove_req(req).m_op_context;
}

/////////////////////////// 3: Get Operations /////////////////////////////////////
struct BtreeSingleGetRequest : public BtreeRequest {
public:
    BtreeSingleGetRequest(std::unique_ptr< const BtreeKey > k, std::unique_ptr< BtreeValue > out_val) :
            m_k{std::move(k)}, m_outval{std::move(out_val)} {}

    const BtreeKey& key() const { return *m_k; }
    const BtreeValue& value() const { return *m_outval; }

    std::unique_ptr< const BtreeKey > m_k;
    std::unique_ptr< BtreeValue > m_outval;
};

struct BtreeGetAnyRequest : public BtreeRequest {
public:
    BtreeGetAnyRequest(BtreeKeyRange&& range, std::unique_ptr< BtreeKey > out_key,
                       std::unique_ptr< BtreeValue > out_val) :
            m_range{std::move(range)}, m_outkey{std::move(out_key)}, m_outval{std::move(out_val)} {}

    BtreeKeyRange m_range;
    std::unique_ptr< BtreeKey > m_outkey;
    std::unique_ptr< BtreeValue > m_outval;
};

using BtreeGetRequest = std::variant< BtreeSingleGetRequest, BtreeGetAnyRequest >;

static bool is_get_any_request(BtreeGetRequest& req) { return (std::holds_alternative< BtreeGetAnyRequest >(req)); }

static BtreeSingleGetRequest& to_single_get_req(BtreeGetRequest& req) { return std::get< BtreeSingleGetRequest >(req); }

static BtreeGetAnyRequest& to_get_any_req(BtreeGetRequest& req) { return std::get< BtreeGetAnyRequest >(req); }

static void* get_req_op_ctx(BtreeGetRequest& req) {
    return (is_get_any_request(req)) ? to_get_any_req(req).m_op_context : to_single_get_req(req).m_op_context;
}

/////////////////////////// 4 Range Query Operations /////////////////////////////////////
ENUM(BtreeQueryType, uint8_t,
     // This is default query which walks to first element in range, and then sweeps/walks
     // across the leaf nodes. However, if upon pagination, it again walks down the query from
     // the key it left off.
     SWEEP_NON_INTRUSIVE_PAGINATION_QUERY,

     // Similar to sweep query, except that it retains the node and its lock during
     // pagination. This is more of intrusive query and if the caller is not careful, the read
     // lock will never be unlocked and could cause deadlocks. Use this option carefully.
     SWEEP_INTRUSIVE_PAGINATION_QUERY,

     // This is relatively inefficient query where every leaf node goes from its parent node
     // instead of walking the leaf node across. This is useful only if we want to check and
     // recover if parent and leaf node are in different generations or crash recovery cases.
     TREE_TRAVERSAL_QUERY,

     // This is both inefficient and quiet intrusive/unsafe query, where it locks the range
     // that is being queried for and do not allow any insert or update within that range. It
     // essentially create a serializable level of isolation.
     SERIALIZABLE_QUERY)

struct BtreeQueryRequest : public BtreeRangeRequest {
public:
    /* TODO :- uint32_max to c++. pass reference */
    BtreeQueryRequest(BtreeSearchState&& search_state,
                      BtreeQueryType query_type = BtreeQueryType::SWEEP_NON_INTRUSIVE_PAGINATION_QUERY,
                      uint32_t batch_size = UINT32_MAX, void* app_context = nullptr) :
            BtreeRangeRequest(std::move(search_state), app_context, batch_size), m_query_type(query_type) {}
    ~BtreeQueryRequest() = default;

    // virtual bool is_serializable() const = 0;
    BtreeQueryType query_type() const { return m_query_type; }

protected:
    const BtreeQueryType m_query_type; // Type of the query
};

/* This class is a top level class to keep track of the locks that are held currently. It is
 * used for serializabke query to unlock all nodes in right order at the end of the lock */
class BtreeLockTracker {
public:
    virtual ~BtreeLockTracker() = default;
};

#if 0
class BtreeSweepQueryRequest : public BtreeQueryRequest {
public:
    BtreeSweepQueryRequest(const BtreeSearchRange& criteria, uint32_t iter_count = 1000,
            const match_item_cb_t& match_item_cb = nullptr) :
            BtreeQueryRequest(criteria, iter_count, match_item_cb) {}

    BtreeSweepQueryRequest(const BtreeSearchRange &criteria, const match_item_cb_t& match_item_cb) :
            BtreeQueryRequest(criteria, 1000, match_item_cb) {}

    bool is_serializable() const { return false; }
};

class BtreeSerializableQueryRequest : public BtreeQueryRequest {
public:
    BtreeSerializableQueryRequest(const BtreeSearchRange &range, uint32_t iter_count = 1000,
                             const match_item_cb_t& match_item_cb = nullptr) :
            BtreeQueryRequest(range, iter_count, match_item_cb) {}

    BtreeSerializableQueryRequest(const BtreeSearchRange &criteria, const match_item_cb_t& match_item_cb) :
            BtreeSerializableQueryRequest(criteria, 1000, match_item_cb) {}

    bool is_serializable() const { return true; }
};
#endif
} // namespace btree
} // namespace sisl
