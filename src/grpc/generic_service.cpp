/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/

#include "sisl/grpc/generic_service.hpp"
#include "utils.hpp"

SISL_LOGGING_DECL(grpc_server)

namespace sisl {

GenericRpcStaticInfo::GenericRpcStaticInfo(GrpcServer* server, grpc::AsyncGenericService* service) :
        m_server{server}, m_generic_service{service} {}

RpcDataAbstract* GenericRpcData::make(GenericRpcStaticInfo* rpc_info, size_t queue_idx) {
    return new GenericRpcData(rpc_info, queue_idx);
}

RpcDataAbstract* GenericRpcData::create_new() { return new GenericRpcData(m_rpc_info, m_queue_idx); }

void GenericRpcData::set_status(grpc::Status& status) { m_retstatus = status; }

GenericRpcData::~GenericRpcData() {
    if (m_request_blob_allocated) { m_request_blob.buf_free(); }
}

size_t GenericRpcData::get_rpc_idx() const { return 0; }

const grpc::ByteBuffer& GenericRpcData::request() const { return m_request; }

sisl::io_blob& GenericRpcData::request_blob() {
    if (m_request_blob.cbytes() == nullptr) {
        if (auto status = try_deserialize_from_byte_buffer(m_request, m_request_blob);
            status.error_code() == grpc::StatusCode::FAILED_PRECONDITION) {
            if (status = deserialize_from_byte_buffer(m_request, m_request_blob); status.ok()) {
                m_request_blob_allocated = true;
            } else {
                LOGERRORMOD(grpc_server, "Failed to deserialize request: code: {}. msg: {}",
                            static_cast< int >(status.error_code()), status.error_message());
            }
        } else if (!status.ok()) {
            LOGERRORMOD(grpc_server, "Failed to try deserialize request: code: {}. msg: {}",
                        static_cast< int >(status.error_code()), status.error_message());
        }
    }
    return m_request_blob;
}

grpc::ByteBuffer& GenericRpcData::response() { return m_response; }

void GenericRpcData::enqueue_call_request(::grpc::ServerCompletionQueue& cq) {
    m_rpc_info->m_generic_service->RequestCall(&m_ctx, &m_stream, &cq, &cq,
                                               static_cast< void* >(m_request_received_tag.ref()));
}

void GenericRpcData::send_response() { m_stream.Write(m_response, static_cast< void* >(m_buf_write_tag.ref())); }
void GenericRpcData::send_response(io_blob_list_t const& response_blob_list) {
    serialize_to_byte_buffer(response_blob_list, m_response);
    send_response();
}

void GenericRpcData::set_context(generic_rpc_ctx_ptr ctx) { m_rpc_context = std::move(ctx); }

GenericRpcContextBase* GenericRpcData::get_context() { return m_rpc_context.get(); }

void GenericRpcData::set_comp_cb(generic_rpc_completed_cb_t const& comp_cb) { m_comp_cb = comp_cb; }

GenericRpcData::GenericRpcData(GenericRpcStaticInfo* rpc_info, size_t queue_idx) :
        RpcDataAbstract{queue_idx}, m_rpc_info{rpc_info}, m_stream(&m_ctx) {}

bool GenericRpcData::do_authorization() {
    m_retstatus = RPCHelper::do_authorization(m_rpc_info->m_server, &m_ctx);
    return m_retstatus.error_code() == grpc::StatusCode::OK;
}

RpcDataAbstract* GenericRpcData::on_request_received(bool ok) {
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

RpcDataAbstract* GenericRpcData::on_buf_read(bool ok) {
    if (ok) {
        auto this_rpc_data = boost::intrusive_ptr< GenericRpcData >{this};
        // take a ref before the handler cb is called.
        // unref is called in send_response which is handled by us (in case of sync calls)
        // or by the handler (for async calls)
        ref();
        if (RPCHelper::run_generic_handler_cb(m_rpc_info->m_server, m_ctx.method(), this_rpc_data)) { send_response(); }
    }
    return nullptr;
}

RpcDataAbstract* GenericRpcData::on_buf_write(bool) {
    m_stream.Finish(m_retstatus, static_cast< void* >(m_request_completed_tag.ref()));
    unref();
    return nullptr;
}

RpcDataAbstract* GenericRpcData::on_request_completed(bool) {
    auto this_rpc_data = boost::intrusive_ptr< GenericRpcData >{this};
    if (m_comp_cb) { m_comp_cb(this_rpc_data); }
    return nullptr;
}

using callback_type = RpcDataAbstract* (GenericRpcData::*)(bool);
GenericRpcData::RpcTagImpl::RpcTagImpl(GenericRpcData* rpc, callback_type cb) : RpcTag{rpc}, m_callback{cb} {}

RpcDataAbstract* GenericRpcData::RpcTagImpl::do_process(bool ok) {
    return (static_cast< GenericRpcData* >(m_rpc_data)->*m_callback)(ok);
}

} // namespace sisl
