
#pragma once

#include <fstream>
#include <sstream>
#include <iostream>
#include <memory>
#include <string>
#include <list>
#include <chrono>
#include <thread>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <boost/assert.hpp>

#include <boost/core/noncopyable.hpp>
#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/codegen/async_unary_call.h>
#include <grpc/support/log.h>

#include "utils.h"

namespace sds::grpc {

using ::grpc::Channel;
using ::grpc::ClientAsyncResponseReader;
using ::grpc::ClientContext;
using ::grpc::CompletionQueue;
using ::grpc::Status;

using namespace ::std::chrono;

/**
 * A interface for handling gRPC async response
 */
class ClientCallMethod : private boost::noncopyable {
public:
    virtual ~ClientCallMethod() {}

    virtual void handle_response(bool ok = true) = 0;
};

/**
 * The specialized 'ClientCallMethod' per gRPC call, it stores
 * the response handler function
 *
 */
template < typename TREQUEST, typename TREPLY >
class ClientCallData final : public ClientCallMethod {

    using handle_response_cb_t = std::function< void(TREPLY&, ::grpc::Status& status) >;

    using ResponseReaderType = std::unique_ptr<::grpc::ClientAsyncResponseReaderInterface< TREPLY > >;

private:
    /* Allow GrpcAsyncClient and its inner classes to use
     * ClientCallData.
     */
    friend class GrpcAsyncClient;

    ClientCallData(handle_response_cb_t handle_response_cb) : handle_response_cb_(handle_response_cb) {}

    // TODO: support time in any time unit -- lhuang8
    void set_deadline(uint32_t seconds) {
        system_clock::time_point deadline = system_clock::now() + std::chrono::seconds(seconds);
        context_.set_deadline(deadline);
    }

    ResponseReaderType& responder_reader() { return response_reader_; }

    Status& status() { return status_; }

    TREPLY& reply() { return reply_; }

    ClientContext& context() { return context_; }

    virtual void handle_response([[maybe_unused]] bool ok = true) override {
        // For unary call, ok is always true, `status_` will indicate error
        // if there are any.
        handle_response_cb_(reply_, status_);
    }

private:
    handle_response_cb_t handle_response_cb_;
    TREPLY reply_;
    ClientContext context_;
    Status status_;
    ResponseReaderType response_reader_;
};

/**
 * A GrpcBaseClient takes care of establish a channel to grpc
 * server. The channel can be used by any number of grpc
 * generated stubs.
 *
 */
class GrpcBaseClient {
protected:
    const std::string server_addr_;
    const std::string target_domain_;
    const std::string ssl_cert_;

    std::shared_ptr<::grpc::ChannelInterface > channel_;

public:
    GrpcBaseClient(const std::string& server_addr, const std::string& target_domain = "",
                   const std::string& ssl_cert = "") :
            server_addr_(server_addr), target_domain_(target_domain), ssl_cert_(ssl_cert) {}

    virtual ~GrpcBaseClient() = default;

    virtual bool init();
    virtual bool is_connection_ready();

private:
    virtual bool init_channel();

    virtual bool load_ssl_cert(const std::string& ssl_cert, std::string& content);
};

class GrpcSyncClient : public GrpcBaseClient {
public:
    using GrpcBaseClient::GrpcBaseClient;

    template < typename TSERVICE >
    std::unique_ptr< typename TSERVICE::StubInterface > MakeStub() {
        return TSERVICE::NewStub(channel_);
    }
};

/**
 *  One GrpcBaseClient can have multiple stub
 *
 * The gRPC client worker, it owns a CompletionQueue and one or more threads,
 * it's only used for handling asynchronous responses.
 *
 * The CompletionQueue is used to send asynchronous request, then the
 * response will be handled on worker threads.
 *
 */
class GrpcAyncClientWorker final {

    enum class State { VOID, INIT, RUNNING, SHUTTING_DOWN, TERMINATED };

public:
    using UPtr = std::unique_ptr< GrpcAyncClientWorker >;

    GrpcAyncClientWorker();
    ~GrpcAyncClientWorker();

    bool run(uint32_t num_threads);

    CompletionQueue& cq() { return completion_queue_; }

    /**
     * Create a GrpcAyncClientWorker.
     *
     */
    static bool create_worker(const char* name, int num_thread);

    /**
     *
     * Get a pointer of GrpcAyncClientWorker by name.
     */
    static GrpcAyncClientWorker* get_worker(const char* name);

    /**
     * Must be called explicitly before program exit if any worker created.
     */
    static void shutdown_all();

private:
    /*
     * Shutdown CompletionQueue and threads.
     *
     * For now, workers can only by shutdown by
     * GrpcAyncClientWorker::shutdown_all().
     */
    void shutdown();

    void async_complete_rpc();

    static std::mutex mutex_workers;
    static std::unordered_map< const char*, GrpcAyncClientWorker::UPtr > workers;

    State state_ = State::VOID;
    CompletionQueue completion_queue_;
    std::list< std::shared_ptr< std::thread > > threads_;
};

class GrpcAsyncClient : public GrpcBaseClient {
public:
    template < typename TSERVICE >
    using StubPtr = std::unique_ptr< typename TSERVICE::StubInterface >;

    /**
     * AsyncStub is a wrapper of generated service stub.
     *
     * An AsyncStub is created with a GrpcAyncClientWorker, all responses
     * of grpc async calls made on it will be handled on the
     * GrpcAyncClientWorker's threads.
     *
     * Please use GrpcAsyncClient::make_stub() to create AsyncStub.
     *
     */
    template < typename TSERVICE >
    struct AsyncStub {
        using UPtr = std::unique_ptr< AsyncStub >;

        AsyncStub(StubPtr< TSERVICE > stub, GrpcAyncClientWorker* worker) : stub_(std::move(stub)), worker_(worker) {}

        using stub_t = typename TSERVICE::StubInterface;

        /* unary call helper */
        template < typename TRESPONSE >
        using unary_call_return_t = std::unique_ptr<::grpc::ClientAsyncResponseReaderInterface< TRESPONSE > >;

        template < typename TREQUEST, typename TRESPONSE >
        using unary_call_t = unary_call_return_t< TRESPONSE > (stub_t::*)(::grpc::ClientContext*, const TREQUEST&,
                                                                          ::grpc::CompletionQueue*);

        template < typename TREQUEST, typename TRESPONSE >
        using unary_callback_t = std::function< void(TRESPONSE&, ::grpc::Status& status) >;

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
         *
         */
        template < typename TREQUEST, typename TRESPONSE >
        void call_unary(const TREQUEST& request, unary_call_t< TREQUEST, TRESPONSE > call,
                        unary_callback_t< TREQUEST, TRESPONSE > callback) {

            auto data = new ClientCallData< TREQUEST, TRESPONSE >(callback);
            // Note that async unary RPCs don't post a CQ tag in call
            data->responder_reader() = (stub_.get()->*call)(&data->context(), request, cq());
            // CQ tag posted here
            data->responder_reader()->Finish(&data->reply(), &data->status(), (void*)data);

            return;
        }

        StubPtr< TSERVICE > stub_;
        GrpcAyncClientWorker* worker_;

        const StubPtr< TSERVICE >& stub() { return stub_; }

        CompletionQueue* cq() { return &worker_->cq(); }
    };

    template < typename T, typename... Ts >
    static auto make(Ts&&... params) {
        std::unique_ptr< T > ret;

        if (!std::is_base_of< GrpcAsyncClient, T >::value) { return ret; }

        ret = std::make_unique< T >(std::forward< Ts >(params)...);
        if (!ret->init()) {
            ret.reset(nullptr);
            return ret;
        }

        return ret;
    }

    template < typename TSERVICE >
    auto make_stub(const char* worker) {

        typename AsyncStub< TSERVICE >::UPtr ret;

        auto w = GrpcAyncClientWorker::get_worker(worker);
        BOOST_ASSERT(w);
        if (!w) {
            return ret; // null
        }

        auto stub = TSERVICE::NewStub(channel_);
        ret = std::make_unique< AsyncStub< TSERVICE > >(std::move(stub), w);
        return ret;
    }

    GrpcAsyncClient(const std::string& server_addr, const std::string& target_domain = "",
                    const std::string& ssl_cert = "") :
            GrpcBaseClient(server_addr, target_domain, ssl_cert) {}

    virtual ~GrpcAsyncClient() {}
};

} // end of namespace sds::grpc
