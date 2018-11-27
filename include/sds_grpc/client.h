
#pragma once

#include <fstream>
#include <sstream>
#include <iostream>
#include <memory>
#include <string>
#include <list>
#include  <chrono>
#include <thread>

#include <grpc++/grpc++.h>
#include <grpcpp/impl/codegen/async_unary_call.h>
#include <grpc/support/log.h>
#include <sds_logging/logging.h>

#include "utils.h"

namespace sds::grpc {

using ::grpc::Channel;
using ::grpc::ClientAsyncResponseReader;
using ::grpc::ClientContext;
using ::grpc::CompletionQueue;
using ::grpc::Status;

using namespace ::std::chrono;

/**
 * ClientCallMethod : Stores the response handler and method name of the rpc
 *
 */
class ClientCallMethod {
  public:
    virtual ~ClientCallMethod() {}

    virtual void handle_response() = 0;
};


/**
 * The specialized 'ClientCallMethod' per-response type.
 *
 */
template<typename TREQUEST, typename TREPLY>
class ClientCallData final : public ClientCallMethod {

    using handle_response_cb_t = std::function<
                                 void(TREPLY&, ::grpc::Status& status)>;

    using ResponseReaderType = std::unique_ptr<
                               ::grpc::ClientAsyncResponseReaderInterface<TREPLY>>;

  public:
    ClientCallData(handle_response_cb_t handle_response_cb)
        : handle_response_cb_(handle_response_cb) { }

    void set_deadline(uint32_t seconds) {
        system_clock::time_point deadline = system_clock::now() +
                                            std::chrono::seconds(seconds);
        context_.set_deadline(deadline);
    }

    ResponseReaderType& responder_reader() {
        return response_reader_;
    }

    Status & status() {
        return status_;
    }

    TREPLY & reply() {
        return reply_;
    }

    ClientContext & context() {
        return context_;
    }


    virtual void handle_response() override {
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
 * A gRPC connection, holds a gRPC Service's stub which used to send gRPC request.
 *
 */
template<typename TSERVICE>
class GrpcConnection {
  public:

    const std::string& server_addr_;
    const std::string& target_domain_;
    const std::string ssl_cert_;

    uint32_t  dead_line_;
    std::shared_ptr<::grpc::ChannelInterface> channel_;
    CompletionQueue*  completion_queue_;
    std::unique_ptr<typename TSERVICE::StubInterface> stub_;


    GrpcConnection(const std::string& server_addr, uint32_t dead_line,
                   CompletionQueue* cq, const std::string& target_domain,
                   const std::string& ssl_cert)
        : server_addr_(server_addr), target_domain_(target_domain),
          ssl_cert_(ssl_cert), dead_line_(dead_line),
          completion_queue_(cq) {

    }

    ~GrpcConnection() { }

    typename TSERVICE::StubInterface* stub() {
        return stub_.get();
    }

    virtual bool init() {
        if (!init_channel()) {
            return false;
        }

        init_stub();
        return true;
    }

    CompletionQueue*  completion_queue() {
        return completion_queue_;
    }


  protected:

    virtual bool init_channel() {

        ::grpc::SslCredentialsOptions ssl_opts;

        if (!ssl_cert_.empty()) {

            if (load_ssl_cert(ssl_cert_, ssl_opts.pem_root_certs)) {
                ::grpc::ChannelArguments channel_args;
                channel_args.SetSslTargetNameOverride(target_domain_);
                channel_ = ::grpc::CreateCustomChannel(server_addr_,
                                                       ::grpc::SslCredentials(ssl_opts),
                                                       channel_args);
            } else {
                // TODO: add log -- lhuang8
                return false;
            }
        } else {
            channel_ = ::grpc::CreateChannel(server_addr_,
                                             ::grpc::InsecureChannelCredentials());
        }

        return true;
    }

    virtual void init_stub() {
        stub_ = TSERVICE::NewStub(channel_);
    }


    virtual bool load_ssl_cert(const std::string& ssl_cert, std::string content) {
        return ::sds::grpc::get_file_contents(ssl_cert, content);;
    }

    virtual bool is_connection_ready() {
        if (channel_->GetState(true) == grpc_connectivity_state::GRPC_CHANNEL_READY)
            return true;
        else
            return false;
    }

    virtual void wait_for_connection_ready() {
        grpc_connectivity_state state;
        int count = 0;
        while ((state = channel_->GetState(true)) != GRPC_CHANNEL_READY && count++ < 5000) {
            usleep(10000);
        }
    }

};


/**
 *
 * Use GrpcConnectionFactory::Make() to create instance of
 * GrpcConnection.
 *
 * TODO: This factory is not good enough, should be refactored
 *       with GrpcConnection and GrpcClient later -- lhuang8
 *
 */
class GrpcConnectionFactory {

  public:
    template<typename T>
    static std::unique_ptr<T> Make(
        const std::string& server_addr, uint32_t dead_line,
        CompletionQueue* cq, const std::string& target_domain,
        const std::string& ssl_cert) {

        std::unique_ptr<T> ret(new T(server_addr, dead_line, cq,
                                     target_domain, ssl_cert));
        if (!ret->init()) {
            ret.reset(nullptr);
        }

        return ret;
    }

};


/**
 * TODO: inherit GrpcConnection and implement as async client -- lhuang8
 * TODO: When work as a async responses handling worker, it's can be hidden from
 *       user of this lib.
 *
 * The gRPC client, it owns a CompletionQueue and one or more threads, it's only
 * used for handling asynchronous responses.
 *
 * The CompletionQueue is used to send asynchronous request, then the
 * response will be handled on this client's threads.
 *
 */
class GrpcClient {
  public:
    GrpcClient() : shutdown_(true) {}

    ~GrpcClient() {
        shutdown();
        for (auto& it : threads_) {
            it->join();
        }
    }

    void shutdown() {
        if (!shutdown_) {
            completion_queue_.Shutdown();
            shutdown_ = true;
        }
    }

    bool run(uint32_t num_threads) {
        if (num_threads == 0) {
            return false;
        }

        shutdown_ = false;
        for (uint32_t i = 0; i < num_threads; ++i) {
            // TODO: no need to call async_complete_rpc for sync calls;
            std::shared_ptr<std::thread> t = std::shared_ptr<std::thread>(
                                                 new std::thread(&GrpcClient::async_complete_rpc, this));
            threads_.push_back(t);
        }

        return true;
    }

    CompletionQueue& cq() {
        return completion_queue_;
    }

  private:

    void async_complete_rpc() {
        void* tag;
        bool ok = false;
        while (completion_queue_.Next(&tag, &ok)) {
            if (!ok) {
                // Client-side StartCallit not going to the wire. This
                // would happen if the channel is either permanently broken or
                // transiently broken but with the fail-fast option.
                continue;
            }

            // The tag was set by ::grpc::ClientAsyncResponseReader<>::Finish(),
            // it must be a instance of ClientCallMethod.
            //
            // TODO: user of this lib should not have change to set the tag,
            //       need to hide tag from user totally -- lhuang8
            ClientCallMethod* cm = static_cast<ClientCallMethod*>(tag);
            cm->handle_response();
        }
    }

  protected:
    CompletionQueue completion_queue_;

  private:
    bool shutdown_;
    std::list<std::shared_ptr<std::thread>> threads_;
};




} // end of namespace sds::grpc
