#pragma once

#include <grpcpp/generic/async_generic_service.h>
#include "rpc_call.hpp"

namespace grpc_helper {

using generic_rpc_handler_cb_t = std::function< bool(boost::intrusive_ptr< GenericRpcData >&) >;

/**
 * Callbacks are registered by a name. The client generic stub uses the method name to call the RPC
 * We assume the Request and Response types are grpc::ByteBuffer
 * The user is responsible to serialize / deserialize their messages to and from grpc::ByteBuffer
 */

class GenericRpcStaticInfo : public RpcStaticInfoBase {
public:
    GenericRpcStaticInfo(GrpcServer* server, grpc::AsyncGenericService* service) :
            m_server{server}, m_generic_service{service} {}

    GrpcServer* m_server;
    grpc::AsyncGenericService* m_generic_service;
};

class GenericRpcData : public RpcDataAbstract, sisl::ObjLifeCounter< GenericRpcData > {
public:
    static RpcDataAbstract* make(GenericRpcStaticInfo* rpc_info, size_t queue_idx) {
        return new GenericRpcData(rpc_info, queue_idx);
    }

    RpcDataAbstract* create_new() override { return new GenericRpcData(m_rpc_info, m_queue_idx); }
    void set_status(grpc::Status& status) { m_retstatus = status; }

    ~GenericRpcData() override = default;

    // There is only one generic static rpc data for all rpcs.
    size_t get_rpc_idx() const override { return 0; }

    const grpc::ByteBuffer& request() const { return m_request; }
    grpc::ByteBuffer& response() { return m_response; }

    void enqueue_call_request(::grpc::ServerCompletionQueue& cq) override {
        m_rpc_info->m_generic_service->RequestCall(&m_ctx, &m_stream, &cq, &cq,
                                                   static_cast< void* >(m_request_received_tag.ref()));
    }

    void send_response() { m_stream.Write(m_response, static_cast< void* >(m_buf_write_tag.ref())); }

    GenericRpcData(GenericRpcStaticInfo* rpc_info, size_t queue_idx) :
            RpcDataAbstract{queue_idx}, m_rpc_info{rpc_info}, m_stream(&m_ctx) {}

private:
    GenericRpcStaticInfo* m_rpc_info;
    grpc::GenericServerAsyncReaderWriter m_stream;
    grpc::GenericServerContext m_ctx;
    grpc::ByteBuffer m_request;
    grpc::ByteBuffer m_response;
    grpc::Status m_retstatus{grpc::Status::OK};

private:
    bool do_authorization() {
        m_retstatus = RPCHelper::do_authorization(m_rpc_info->m_server, &m_ctx);
        return m_retstatus.error_code() == grpc::StatusCode::OK;
    }

    RpcDataAbstract* on_request_received(bool ok) {
        bool in_shutdown = RPCHelper::has_server_shutdown(m_rpc_info->m_server);

        if (ok) {
            if (!do_authorization()) {
                m_stream.Finish(m_retstatus, static_cast< void* >(m_request_completed_tag.ref()));
            } else {
                m_stream.Read(&m_request, static_cast< void* >(m_buf_read_tag.ref()));
            }
        }

        return in_shutdown ? nullptr : create_new();
    }

    RpcDataAbstract* on_buf_read(bool ok) {
        auto this_rpc_data = boost::intrusive_ptr< GenericRpcData >{this};
        // take a ref before the handler cb is called.
        // unref is called in send_response which is handled by us (in case of sync calls)
        // or by the handler (for async calls)
        ref();
        if (RPCHelper::run_generic_handler_cb(m_rpc_info->m_server, m_ctx.method(), this_rpc_data)) { send_response(); }
        return nullptr;
    }

    RpcDataAbstract* on_buf_write(bool ok) {
        m_stream.Finish(m_retstatus, static_cast< void* >(m_request_completed_tag.ref()));
        unref();
        return nullptr;
    }

    RpcDataAbstract* on_request_completed(bool ok) { return nullptr; }

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