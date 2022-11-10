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

#include <sisl/logging/logging.h>
#include <sisl/utility/obj_life_counter.hpp>
#include <sisl/utility/enum.hpp>
#include <sisl/auth_manager/trf_client.hpp>

namespace grpc_helper {

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
class ClientRpcDataInternal;

using GenericClientRpcData = ClientRpcData< grpc::ByteBuffer, grpc::ByteBuffer >;
using generic_rpc_comp_cb_t = rpc_comp_cb_t< grpc::ByteBuffer, grpc::ByteBuffer >;
using generic_req_builder_cb_t = req_builder_cb_t< grpc::ByteBuffer >;
using generic_unary_callback_t = unary_callback_t< grpc::ByteBuffer >;
using GenericClientRpcDataInternal = ClientRpcDataInternal< grpc::ByteBuffer, grpc::ByteBuffer >;

/**
 * The specialized 'ClientRpcDataInternal' per gRPC call, it stores
 * the response handler function
 *
 */
template < typename ReqT, typename RespT >
class ClientRpcDataInternal : public ClientRpcDataAbstract {
public:
    using ResponseReaderPtr = std::unique_ptr< ::grpc::ClientAsyncResponseReaderInterface< RespT > >;
    using GenericResponseReaderPtr = std::unique_ptr< grpc::GenericClientAsyncResponseReader >;

    /* Allow GrpcAsyncClient and its inner classes to use
     * ClientCallData.
     */
    friend class GrpcAsyncClient;

    ClientRpcDataInternal() = default;
    ClientRpcDataInternal(const unary_callback_t< RespT >& cb) : m_cb{cb} {}
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

    virtual void handle_response([[maybe_unused]] bool ok = true) override {
        // For unary call, ok is always true, `status_` will indicate error if there are any.
        m_cb(m_reply, m_status);
    }

    void add_metadata(const std::string& meta_key, const std::string& meta_value) {
        m_context.AddMetadata(meta_key, meta_value);
    }

    unary_callback_t< RespT > m_cb;
    RespT m_reply;
    ::grpc::ClientContext m_context;
    ::grpc::Status m_status;
    ResponseReaderPtr m_resp_reader_ptr;
    GenericResponseReaderPtr m_generic_resp_reader_ptr;
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

    std::shared_ptr< ::grpc::ChannelInterface > m_channel;
    std::shared_ptr< sisl::TrfClient > m_trf_client;

public:
    GrpcBaseClient(const std::string& server_addr, const std::string& target_domain = "",
                   const std::string& ssl_cert = "");
    GrpcBaseClient(const std::string& server_addr, const std::shared_ptr< sisl::TrfClient >& trf_client,
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

ENUM(ClientState, uint8_t, VOID, INIT, RUNNING, SHUTTING_DOWN, TERMINATED);

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

class GrpcAsyncClient : public GrpcBaseClient {
public:
    template < typename ServiceT >
    using StubPtr = std::unique_ptr< typename ServiceT::StubInterface >;

    GrpcAsyncClient(const std::string& server_addr, const std::shared_ptr< sisl::TrfClient > trf_client,
                    const std::string& target_domain = "", const std::string& ssl_cert = "") :
            GrpcBaseClient(server_addr, trf_client, target_domain, ssl_cert) {}

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
                  std::shared_ptr< sisl::TrfClient > trf_client) :
                m_stub(std::move(stub)), m_worker(worker), m_trf_client(trf_client) {}

        using stub_t = typename ServiceT::StubInterface;

        /* unary call helper */
        template < typename RespT >
        using unary_call_return_t = std::unique_ptr< ::grpc::ClientAsyncResponseReaderInterface< RespT > >;

        template < typename ReqT, typename RespT >
        using unary_call_t = unary_call_return_t< RespT > (stub_t::*)(::grpc::ClientContext*, const ReqT&,
                                                                      ::grpc::CompletionQueue*);

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
         *
         */
        template < typename ReqT, typename RespT >
        void call_unary(const ReqT& request, const unary_call_t< ReqT, RespT >& method,
                        const unary_callback_t< RespT >& callback, uint32_t deadline) {
            auto data = new ClientRpcDataInternal< ReqT, RespT >(callback);
            data->set_deadline(deadline);
            if (m_trf_client) { data->add_metadata("authorization", m_trf_client->get_typed_token()); }
            // Note that async unary RPCs don't post a CQ tag in call
            data->m_resp_reader_ptr = (m_stub.get()->*method)(&data->context(), request, cq());
            // CQ tag posted here
            data->m_resp_reader_ptr->Finish(&data->reply(), &data->status(), (void*)data);
            return;
        }

        template < typename ReqT, typename RespT >
        void call_rpc(const req_builder_cb_t< ReqT >& builder_cb, const unary_call_t< ReqT, RespT >& method,
                      const rpc_comp_cb_t< ReqT, RespT >& done_cb, uint32_t deadline) {
            auto cd = new ClientRpcData< ReqT, RespT >(done_cb);
            builder_cb(cd->m_req);
            cd->set_deadline(deadline);
            if (m_trf_client) { cd->add_metadata("authorization", m_trf_client->get_typed_token()); }
            cd->m_resp_reader_ptr = (m_stub.get()->*method)(&cd->context(), cd->m_req, cq());
            cd->m_resp_reader_ptr->Finish(&cd->reply(), &cd->status(), (void*)cd);
        }

        StubPtr< ServiceT > m_stub;
        GrpcAsyncClientWorker* m_worker;
        std::shared_ptr< sisl::TrfClient > m_trf_client;

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
                         std::shared_ptr< sisl::TrfClient > trf_client) :
                m_generic_stub(std::move(stub)), m_worker(worker), m_trf_client(trf_client) {}

        void call_unary(const grpc::ByteBuffer& request, const std::string& method,
                        const generic_unary_callback_t& callback, uint32_t deadline);

        void call_rpc(const generic_req_builder_cb_t& builder_cb, const std::string& method,
                      const generic_rpc_comp_cb_t& done_cb, uint32_t deadline);

        std::unique_ptr< grpc::GenericStub > m_generic_stub;
        GrpcAsyncClientWorker* m_worker;
        std::shared_ptr< sisl::TrfClient > m_trf_client;

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

        return std::make_unique< AsyncStub< ServiceT > >(ServiceT::NewStub(m_channel), w, m_trf_client);
    }

    std::unique_ptr< GenericAsyncStub > make_generic_stub(const std::string& worker);
};

} // namespace grpc_helper
