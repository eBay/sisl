#pragma once

#include <grpcpp/generic/async_generic_service.h>
#include "rpc_call.hpp"

namespace grpc_helper {

using generic_rpc_handler_cb_t = std::function< bool(const boost::intrusive_ptr< GenericRpcData >&) >;

/**
 * Callbacks are registered by a name. The client generic stub uses the method name to call the RPC
 * We assume the Request and Response types are grpc::ByteBuffer
 * The user is responsible to serialize / deserialize their messages to and from grpc::ByteBuffer
 */

class GenericRpcStaticInfo : public RpcStaticInfoBase {
public:
    GenericRpcStaticInfo(GrpcServer* server, size_t idx) : m_server{server}, m_rpc_idx{idx} {}

    GrpcServer* m_server;
    grpc::AsyncGenericService m_generic_service;
    size_t m_rpc_idx;
};

class GenericRpcData : public RpcDataAbstract, sisl::ObjLifeCounter< GenericRpcData > {
public:
    static RpcDataAbstract* make(GenericRpcStaticInfo* rpc_info, size_t queue_idx) {
        return new GenericRpcData(rpc_info, queue_idx);
    }

    RpcDataAbstract* create_new() override { return new GenericRpcData(m_rpc_info, m_queue_idx); }
    void set_status(grpc::Status status) { m_retstatus = status; }

    ~GenericRpcData() override = default;

    size_t get_rpc_idx() const override { return m_rpc_info->m_rpc_idx; }

    void enqueue_call_request(::grpc::ServerCompletionQueue& cq) override {
        m_rpc_info->m_generic_service.RequestCall(&m_ctx, &m_stream, &cq, &cq,
                                                  static_cast< void* >(m_request_received_tag.ref()));
    }

    GenericRpcData(GenericRpcStaticInfo* rpc_info, size_t queue_idx) :
            RpcDataAbstract{queue_idx}, m_rpc_info{rpc_info}, m_stream(&m_ctx) {}

private:
    GenericRpcStaticInfo* m_rpc_info;
    grpc::GenericServerAsyncReaderWriter m_stream;
    grpc::GenericServerContext m_ctx;
    grpc::ByteBuffer m_request;
    grpc::ByteBuffer m_response;
    std::atomic_bool m_is_canceled{false};
    grpc::Status m_retstatus{grpc::Status::OK};

private:
    RpcDataAbstract* on_request_received(bool ok) {
        bool in_shutdown = RPCHelper::has_server_shutdown(m_rpc_info->m_server);

        if (ok && !m_is_canceled.load(std::memory_order_relaxed)) {
            m_stream.Read(&m_request, static_cast< void* >(m_buf_read_tag.ref()));
        }

        return in_shutdown ? nullptr : create_new();
    }

    RpcDataAbstract* on_buf_read(bool ok) {
        RPCHelper::run_generic_handler_cb(m_rpc_info->m_server, m_ctx.method(),
                                          boost::intrusive_ptr< GenericRpcData >{this});
        m_stream.Write(m_response, static_cast< void* >(m_buf_write_tag.ref()));
        return nullptr;
    }

    RpcDataAbstract* on_buf_write(bool ok) {
        m_stream.Finish(m_retstatus, static_cast< void* >(m_request_completed_tag.ref()));
        return nullptr;
    }

    RpcDataAbstract* on_request_completed(bool ok) {
        if (m_ctx.IsCancelled()) { m_is_canceled.store(true, std::memory_order_release); }
        return nullptr;
    }

    struct RpcTagImpl : public RpcTag {
        using callback_type = RpcDataAbstract* (GenericRpcData::*)(bool ok);
        RpcTagImpl(GenericRpcData* rpc, callback_type cb) : RpcTag{rpc}, m_callback{cb} {}

        RpcDataAbstract* do_process(bool ok) override {
            return (static_cast< GenericRpcData* >(m_rpc_data)->*m_callback)(ok);
        }

        callback_type m_callback;
    };

    // Used as void* completion markers from grpc to indicate different events of interest for a
    // Call.
    RpcTagImpl m_request_received_tag{this, &GenericRpcData::on_request_received};
    RpcTagImpl m_buf_read_tag{this, &GenericRpcData::on_buf_read};
    RpcTagImpl m_buf_write_tag{this, &GenericRpcData::on_buf_write};
    RpcTagImpl m_request_completed_tag{this, &GenericRpcData::on_request_completed};
};

} // namespace grpc_helper