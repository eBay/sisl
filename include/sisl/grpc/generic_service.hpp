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
#pragma once

#include <grpcpp/generic/async_generic_service.h>
#include "sisl/fds/buffer.hpp"
#include "rpc_call.hpp"

namespace sisl {

/**
 * Callbacks are registered by a name. The client generic stub uses the method name to call the RPC
 * We assume the Request and Response types are grpc::ByteBuffer
 * The user is responsible to serialize / deserialize their messages to and from grpc::ByteBuffer
 */

class GenericRpcStaticInfo : public RpcStaticInfoBase {
public:
    GenericRpcStaticInfo(GrpcServer* server, grpc::AsyncGenericService* service);

    GrpcServer* m_server;
    grpc::AsyncGenericService* m_generic_service;
};

class GenericRpcContextBase {
public:
    virtual ~GenericRpcContextBase() = default;
};
using generic_rpc_ctx_ptr = std::unique_ptr< GenericRpcContextBase >;

class GenericRpcData : public RpcDataAbstract, sisl::ObjLifeCounter< GenericRpcData > {
public:
    static RpcDataAbstract* make(GenericRpcStaticInfo* rpc_info, size_t queue_idx);

    RpcDataAbstract* create_new() override;
    void set_status(grpc::Status& status);

    ~GenericRpcData() override;

    // There is only one generic static rpc data for all rpcs.
    size_t get_rpc_idx() const override;

    const grpc::ByteBuffer& request() const;
    sisl::io_blob& request_blob();

    grpc::ByteBuffer& response();

    void enqueue_call_request(::grpc::ServerCompletionQueue& cq) override;

    void send_response();
    void send_response(io_blob_list_t const& response_blob_list);

    void set_context(generic_rpc_ctx_ptr ctx);
    GenericRpcContextBase* get_context();

    void set_comp_cb(generic_rpc_completed_cb_t const& comp_cb);

    GenericRpcData(GenericRpcStaticInfo* rpc_info, size_t queue_idx);

private:
    GenericRpcStaticInfo* m_rpc_info;
    grpc::GenericServerAsyncReaderWriter m_stream;
    grpc::GenericServerContext m_ctx;
    grpc::ByteBuffer m_request;
    grpc::ByteBuffer m_response;
    sisl::io_blob m_request_blob;
    bool m_request_blob_allocated{false};
    grpc::Status m_retstatus{grpc::Status::OK};
    // user can set and retrieve the context. Its life cycle is tied to that of rpc data.
    generic_rpc_ctx_ptr m_rpc_context;
    // the handler cb can fill in the completion cb if it needs one
    generic_rpc_completed_cb_t m_comp_cb{nullptr};

private:
    bool do_authorization();

    RpcDataAbstract* on_request_received(bool ok);
    RpcDataAbstract* on_buf_read(bool ok);
    RpcDataAbstract* on_buf_write(bool);
    RpcDataAbstract* on_request_completed(bool);

    struct RpcTagImpl : public RpcTag {
        using callback_type = RpcDataAbstract* (GenericRpcData::*)(bool);
        RpcTagImpl(GenericRpcData* rpc, callback_type cb);

        RpcDataAbstract* do_process(bool ok) override;

        callback_type m_callback;
    };

    // Used as void* completion markers from grpc to indicate different events of interest for a
    // Call.
    RpcTagImpl m_request_received_tag{this, &GenericRpcData::on_request_received};
    RpcTagImpl m_buf_read_tag{this, &GenericRpcData::on_buf_read};
    RpcTagImpl m_buf_write_tag{this, &GenericRpcData::on_buf_write};
    RpcTagImpl m_request_completed_tag{this, &GenericRpcData::on_request_completed};
};

} // namespace sisl
