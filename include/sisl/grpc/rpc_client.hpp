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

#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <type_traits>
#include <unordered_map>

#include <boost/core/noncopyable.hpp>
#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/codegen/async_unary_call.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpc/support/log.h>
#include <folly/futures/Future.h>

#include <sisl/logging/logging.h>
#include <sisl/utility/obj_life_counter.hpp>
#include <sisl/utility/enum.hpp>
#include <sisl/auth_manager/token_client.hpp>
#include <sisl/fds/buffer.hpp>

#include <fmt/format.h>

namespace grpc {
inline auto format_as(StatusCode s) { return fmt::underlying(s); }
} // namespace grpc

namespace sisl {

/**
 * A interface for handling gRPC async response
 */
class ClientRpcDataAbstract : private boost::noncopyable {
public:
    virtual ~ClientRpcDataAbstract() = default;
    virtual void handle_response(bool ok = true) = 0;
};

template < typename ReqT, typename RespT >
class ClientRpcData;

template < typename ReqT, typename RespT >
using rpc_comp_cb_t = std::function< void(ClientRpcData< ReqT, RespT >& cd) >;

template < typename ReqT >
using req_builder_cb_t = std::function< void(ReqT&) >;

template < typename RespT >
using unary_callback_t = std::function< void(RespT&, ::grpc::Status& status) >;

template < typename ReqT, typename RespT >
class ClientRpcDataCallback;

template < typename ReqT, typename RespT >
class ClientRpcDataFuture;

template < typename ReqT, typename RespT >
class ClientRpcDataFutureBlob;

template < typename T >
using Result = folly::Expected< T, ::grpc::Status >;

template < typename T >
using AsyncResult = folly::SemiFuture< Result< T > >;

using GenericClientRpcData = ClientRpcData< grpc::ByteBuffer, grpc::ByteBuffer >;
using generic_rpc_comp_cb_t = rpc_comp_cb_t< grpc::ByteBuffer, grpc::ByteBuffer >;
using generic_req_builder_cb_t = req_builder_cb_t< grpc::ByteBuffer >;
using generic_unary_callback_t = unary_callback_t< grpc::ByteBuffer >;
using GenericClientRpcDataCallback = ClientRpcDataCallback< grpc::ByteBuffer, grpc::ByteBuffer >;
using GenericClientRpcDataFuture = ClientRpcDataFuture< grpc::ByteBuffer, grpc::ByteBuffer >;

/**
 * The specialized 'ClientRpcDataInternal' per gRPC call,
 * Derive from this class to create Rpc Data that can hold
 * the response handler function or a promise
 */
template < typename ReqT, typename RespT >
class ClientRpcDataInternal : public ClientRpcDataAbstract {
public:
    using ResponseReaderPtr = std::unique_ptr<::grpc::ClientAsyncResponseReaderInterface< RespT > >;
    using GenericResponseReaderPtr = std::unique_ptr< grpc::GenericClientAsyncResponseReader >;

    /* Allow GrpcAsyncClient and its inner classes to use
     * ClientCallData.
     */
    friend class GrpcAsyncClient;

    ClientRpcDataInternal() = default;
    virtual ~ClientRpcDataInternal() = default;

    // TODO: support time in any time unit -- lhuang8
    void set_deadline(uint32_t seconds) {
        std::chrono::system_clock::time_point deadline =
            std::chrono::system_clock::now() + std::chrono::seconds(seconds);
        m_context.set_deadline(deadline);
    }

    ResponseReaderPtr& responder_reader() { return m_resp_reader_ptr; }
    ::grpc::Status& status() { return m_status; }
    RespT& reply() { return m_reply; }
    ::grpc::ClientContext& context() { return m_context; }

    virtual void handle_response(bool ok = true) = 0;

    void add_metadata(const std::string& meta_key, const std::string& meta_value) {
        m_context.AddMetadata(meta_key, meta_value);
    }

    RespT m_reply;
    ::grpc::ClientContext m_context;
    ::grpc::Status m_status;
    ResponseReaderPtr m_resp_reader_ptr;
    GenericResponseReaderPtr m_generic_resp_reader_ptr;
};

/**
 * callback version of ClientRpcDataInternal
 */
template < typename ReqT, typename RespT >
class ClientRpcDataCallback : public ClientRpcDataInternal< ReqT, RespT > {
public:
    ClientRpcDataCallback(const unary_callback_t< RespT >& cb) : m_cb{cb} {}

    virtual void handle_response([[maybe_unused]] bool ok = true) override {
        // For unary call, ok is always true, `status_` will indicate error if there are any.
        if (m_cb) { m_cb(this->m_reply, this->m_status); }
    }

    unary_callback_t< RespT > m_cb;
};

/**
 * futures version of ClientRpcDataInternal
 */
template < typename ReqT, typename RespT >
class ClientRpcDataFuture : public ClientRpcDataInternal< ReqT, RespT > {
public:
    ClientRpcDataFuture(folly::Promise< Result< RespT > >&& promise) : m_promise{std::move(promise)} {}

    virtual void handle_response([[maybe_unused]] bool ok = true) override {
        // For unary call, ok is always true, `status_` will indicate error if there are any.
        if (this->m_status.ok()) {
            m_promise.setValue(this->m_reply);
        } else {
            m_promise.setValue(folly::makeUnexpected(this->m_status));
        }
    }

    folly::Promise< Result< RespT > > m_promise;
};

class GenericClientResponse {
public:
    GenericClientResponse() = default;
    GenericClientResponse(const grpc::ByteBuffer& buf);
    ~GenericClientResponse();

    io_blob& response_blob();
    void set_allocation_flag() { m_response_blob_allocated = true; }

private:
    grpc::ByteBuffer m_response_buf;
    io_blob m_response_blob;
    bool m_response_blob_allocated{false};
};
using client_response_ptr = std::unique_ptr< GenericClientResponse >;

/**
 * futures version of ClientRpcDataInternal
 * This class holds the promise end of the grpc response
 * that returns a client_response_ptr. The sisl::io_blob version of the response
 * can be accessed via the response_blob() method.
 */
class GenericRpcDataFutureBlob : public ClientRpcDataInternal< grpc::ByteBuffer, grpc::ByteBuffer > {
public:
    GenericRpcDataFutureBlob(folly::Promise< Result< client_response_ptr > >&& promise);
    virtual void handle_response([[maybe_unused]] bool ok = true) override;

private:
    folly::Promise< Result< client_response_ptr > > m_promise;
};

template < typename ReqT, typename RespT >
class ClientRpcData : public ClientRpcDataInternal< ReqT, RespT > {
public:
    ClientRpcData(const rpc_comp_cb_t< ReqT, RespT >& comp_cb) : m_comp_cb{comp_cb} {}
    virtual ~ClientRpcData() = default;

    virtual void handle_response([[maybe_unused]] bool ok = true) override {
        // For unary call, ok is always true, `status_` will indicate error if there are any.
        m_comp_cb(*this);
        // Caller could delete this pointer and thus don't acccess anything after this.
    }

    const ReqT& req() { return m_req; }

    rpc_comp_cb_t< ReqT, RespT > m_comp_cb;
    ReqT m_req;
};

/**
 * A GrpcBaseClient takes care of establish a channel to grpc
 * server. The channel can be used by any number of grpc
 * generated stubs.
 *
 */
class GrpcBaseClient {
protected:
    const std::string m_server_addr;
    const std::string m_target_domain;
    const std::string m_ssl_cert;

    std::shared_ptr<::grpc::ChannelInterface > m_channel;
    std::shared_ptr< sisl::GrpcTokenClient > m_token_client;

public:
    GrpcBaseClient(const std::string& server_addr, const std::string& target_domain = "",
                   const std::string& ssl_cert = "");
    GrpcBaseClient(const std::string& server_addr, const std::shared_ptr< sisl::GrpcTokenClient >& token_client,
                   const std::string& target_domain = "", const std::string& ssl_cert = "");
    virtual ~GrpcBaseClient() = default;
    virtual bool is_connection_ready() const;
    virtual void init();

private:
    virtual bool load_ssl_cert(const std::string& ssl_cert, std::string& content);
};

class GrpcSyncClient : public GrpcBaseClient {
public:
    using GrpcBaseClient::GrpcBaseClient;

    template < typename ServiceT >
    std::unique_ptr< typename ServiceT::StubInterface > MakeStub() {
        return ServiceT::NewStub(m_channel);
    }
};

ENUM(ClientState, uint8_t, VOID, INIT, RUNNING, SHUTTING_DOWN, TERMINATED)

/**
 * One GrpcBaseClient can have multiple stub
 *
 * The gRPC client worker, it owns a CompletionQueue and one or more threads,
 * it's only used for handling asynchronous responses.
 *
 * The CompletionQueue is used to send asynchronous request, then the
 * response will be handled on worker threads.
 *
 */
class GrpcAsyncClientWorker final {
public:
    using UPtr = std::unique_ptr< GrpcAsyncClientWorker >;

    GrpcAsyncClientWorker() = default;
    ~GrpcAsyncClientWorker();

    void run(uint32_t num_threads);

    ::grpc::CompletionQueue& cq() { return m_cq; }

    static void create_worker(const std::string& name, int num_threads);
    static GrpcAsyncClientWorker* get_worker(const std::string& name);

    /**
     * Must be called explicitly before program exit if any worker created.
     */
    static void shutdown_all();

private:
    /*
     * Shutdown CompletionQueue and threads.
     *
     * For now, workers can only by shutdown by
     * GrpcAsyncClientWorker::shutdown_all().
     */
    void shutdown();
    void client_loop();

private:
    static std::mutex s_workers_mtx;
    static std::unordered_map< std::string, GrpcAsyncClientWorker::UPtr > s_workers;

    ClientState m_state{ClientState::INIT};
    ::grpc::CompletionQueue m_cq;
    std::vector< std::thread > m_threads;
};

// common request id header
static std::string const request_id_header{"request_id"};

class GrpcAsyncClient : public GrpcBaseClient {
public:
    template < typename ServiceT >
    using StubPtr = std::unique_ptr< typename ServiceT::StubInterface >;

    GrpcAsyncClient(const std::string& server_addr, const std::shared_ptr< sisl::GrpcTokenClient > token_client,
                    const std::string& target_domain = "", const std::string& ssl_cert = "") :
            GrpcBaseClient(server_addr, token_client, target_domain, ssl_cert) {}

    GrpcAsyncClient(const std::string& server_addr, const std::string& target_domain = "",
                    const std::string& ssl_cert = "") :
            GrpcAsyncClient(server_addr, nullptr, target_domain, ssl_cert) {}

    virtual ~GrpcAsyncClient() {}

    /**
     * AsyncStub is a wrapper of generated service stub.
     *
     * An AsyncStub is created with a GrpcAsyncClientWorker, all responses
     * of grpc async calls made on it will be handled on the
     * GrpcAsyncClientWorker's threads.
     *
     * Please use GrpcAsyncClient::make_stub() to create AsyncStub.
     *
     */
    template < typename ServiceT >
    struct AsyncStub {
        using UPtr = std::unique_ptr< AsyncStub >;

        AsyncStub(StubPtr< ServiceT > stub, GrpcAsyncClientWorker* worker,
                  std::shared_ptr< sisl::GrpcTokenClient > token_client) :
                m_stub(std::move(stub)), m_worker(worker), m_token_client(token_client) {}

        using stub_t = typename ServiceT::StubInterface;

        /* unary call helper */
        template < typename RespT >
        using unary_call_return_t = std::unique_ptr<::grpc::ClientAsyncResponseReaderInterface< RespT > >;

        template < typename ReqT, typename RespT >
        using unary_call_t = unary_call_return_t< RespT > (stub_t::*)(::grpc::ClientContext*, const ReqT&,
                                                                      ::grpc::CompletionQueue*);

        template < typename ReqT, typename RespT >
        void prepare_and_send_unary(ClientRpcDataInternal< ReqT, RespT >* data, const ReqT& request,
                                    const unary_call_t< ReqT, RespT >& method, uint32_t deadline,
                                    const std::vector< std::pair< std::string, std::string > >& metadata) {
            data->set_deadline(deadline);
            for (auto const& [key, value] : metadata) {
                data->add_metadata(key, value);
            }
            if (m_token_client) {
                data->add_metadata(m_token_client->get_auth_header_key(), m_token_client->get_token());
            }
            // Note that async unary RPCs don't post a CQ tag in call
            data->m_resp_reader_ptr = (m_stub.get()->*method)(&data->m_context, request, cq());
            // CQ tag posted here
            data->m_resp_reader_ptr->Finish(&data->reply(), &data->status(), (void*)data);
        }

        // using unary_callback_t = std::function< void(RespT&, ::grpc::Status& status) >;

        /**
         * Make a unary call.
         *
         * @param request - a request of this unary call.
         * @param call - a pointer to a member function in grpc service stub
         *     which used to make an aync call. If service name is
         *     "EchoService" and an unary rpc is defined as:
         *     `    rpc Echo (EchoRequest) returns (EchoReply) {}`
         *     then the member function used here should be:
         *     `EchoService::StubInterface::AsyncEcho`.
         * @param callback - the response handler function, which will be
         *     called after response received asynchronously or call failed(which
         *     would happen if the channel is either permanently broken or
         *     transiently broken, or call timeout).
         *     The callback function must check if `::grpc::Status` argument is
         *     OK before handling the response. If call failed, `::grpc::Status`
         *     indicates the error code and error message.
         * @param deadline - deadline in seconds
         * @param metadata - key value pair of the metadata to be sent with the request
         *
         */
        template < typename ReqT, typename RespT >
        void call_unary(const ReqT& request, const unary_call_t< ReqT, RespT >& method,
                        const unary_callback_t< RespT >& callback, uint32_t deadline,
                        const std::vector< std::pair< std::string, std::string > >& metadata) {
            auto data = new ClientRpcDataCallback< ReqT, RespT >(callback);
            prepare_and_send_unary(data, request, method, deadline, metadata);
        }

        template < typename ReqT, typename RespT >
        void call_unary(const ReqT& request, const unary_call_t< ReqT, RespT >& method,
                        const unary_callback_t< RespT >& callback, uint32_t deadline) {
            call_unary(request, method, callback, deadline, {});
        }

        template < typename ReqT, typename RespT >
        void call_rpc(const req_builder_cb_t< ReqT >& builder_cb, const unary_call_t< ReqT, RespT >& method,
                      const rpc_comp_cb_t< ReqT, RespT >& done_cb, uint32_t deadline) {
            auto cd = new ClientRpcData< ReqT, RespT >(done_cb);
            builder_cb(cd->m_req);
            prepare_and_send_unary(cd, cd->m_req, method, deadline, {});
        }

        // Futures version of call_unary
        template < typename ReqT, typename RespT >
        AsyncResult< RespT > call_unary(const ReqT& request, const unary_call_t< ReqT, RespT >& method,
                                        uint32_t deadline,
                                        const std::vector< std::pair< std::string, std::string > >& metadata) {
            auto [p, sf] = folly::makePromiseContract< Result< RespT > >();
            auto data = new ClientRpcDataFuture< ReqT, RespT >(std::move(p));
            prepare_and_send_unary(data, request, method, deadline, metadata);
            return std::move(sf);
        }

        template < typename ReqT, typename RespT >
        AsyncResult< RespT > call_unary(const ReqT& request, const unary_call_t< ReqT, RespT >& method,
                                        uint32_t deadline) {
            return call_unary(request, method, deadline, {});
        }

        StubPtr< ServiceT > m_stub;
        GrpcAsyncClientWorker* m_worker;
        std::shared_ptr< sisl::GrpcTokenClient > m_token_client;

        const StubPtr< ServiceT >& stub() { return m_stub; }

        ::grpc::CompletionQueue* cq() { return &m_worker->cq(); }
    };

    /**
     * GenericAsyncStub is a wrapper of the grpc::GenericStub which
     * provides the interface to call generic methods by name.
     * We assume the Request and Response types are grpc::ByteBuffer.
     *
     * Please use GrpcAsyncClient::make_generic_stub() to create GenericAsyncStub.
     */

    struct GenericAsyncStub {
        GenericAsyncStub(std::unique_ptr< grpc::GenericStub > stub, GrpcAsyncClientWorker* worker,
                         std::shared_ptr< sisl::GrpcTokenClient > token_client) :
                m_generic_stub(std::move(stub)), m_worker(worker), m_token_client(token_client) {}

        void prepare_and_send_unary_generic(ClientRpcDataInternal< grpc::ByteBuffer, grpc::ByteBuffer >* data,
                                            const grpc::ByteBuffer& request, const std::string& method,
                                            uint32_t deadline);

        void call_unary(const grpc::ByteBuffer& request, const std::string& method,
                        const generic_unary_callback_t& callback, uint32_t deadline);

        void call_rpc(const generic_req_builder_cb_t& builder_cb, const std::string& method,
                      const generic_rpc_comp_cb_t& done_cb, uint32_t deadline);

        // futures version of call_unary
        AsyncResult< grpc::ByteBuffer > call_unary(const grpc::ByteBuffer& request, const std::string& method,
                                                   uint32_t deadline);

        AsyncResult< client_response_ptr > call_unary(const io_blob_list_t& request, const std::string& method,
                                                      uint32_t deadline);

        std::unique_ptr< grpc::GenericStub > m_generic_stub;
        GrpcAsyncClientWorker* m_worker;
        std::shared_ptr< sisl::GrpcTokenClient > m_token_client;

        grpc::CompletionQueue* cq() { return &m_worker->cq(); }
    };

    template < typename T, typename... Ts >
    static auto make(Ts&&... params) {
        return std::make_unique< T >(std::forward< Ts >(params)...);
    }

    template < typename ServiceT >
    auto make_stub(const std::string& worker) {
        auto w = GrpcAsyncClientWorker::get_worker(worker);
        if (w == nullptr) { throw std::runtime_error("worker thread not available"); }

        return std::make_unique< AsyncStub< ServiceT > >(ServiceT::NewStub(m_channel), w, m_token_client);
    }

    std::unique_ptr< GenericAsyncStub > make_generic_stub(const std::string& worker);
};

} // namespace sisl
