
#pragma once

#include <fstream>
#include <sstream>
#include <iostream>
#include <memory>
#include <string>
#include <list>

#include <grpc++/grpc++.h>
#include <grpc/support/log.h>
#include <sds_logging/logging.h>
#include <thread>

#include "utils.h"

namespace sds::grpc
{

class CallbackHandler;
class ClientCallMethod;

using ::grpc::Channel;
using ::grpc::ClientAsyncResponseReader;
using ::grpc::ClientContext;
using ::grpc::CompletionQueue;
using ::grpc::Status;



/**
 * ClientCallMethod : Stores the callback handler and method name of the rpc
 *
 * TODO: rename as BaseClientCallData
 */
class ClientCallMethod {
public:
    ClientCallMethod(CallbackHandler* handler, const std::string& methodName) :
        cb_handler_(handler), method_name_(methodName)
    {}

    virtual ~ClientCallMethod() {}

    const std::string&  call_method_name() { return method_name_; }
    CallbackHandler*    cb_handler() { return cb_handler_; }

protected:

private:
    CallbackHandler*    cb_handler_;
    std::string         method_name_;
};


/**
 * The specialized 'ClientCallMethod' per-response type.
 *
 *
 */
template<typename TREQUEST, typename TREPLY>
class ClientCallData : public ClientCallMethod {
public:
    ClientCallData(CallbackHandler* handler, const std::string& methodName,
    		uint32_t deadlineSeconds)
        : ClientCallMethod(handler, methodName) {
        std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + 
            std::chrono::seconds(deadlineSeconds);
        context_.set_deadline(deadline);
    }

    Status& rpc_status() { return rpc_status_; }

    TREPLY& reply() { return reply_; }
    ClientContext& context() { return context_; }

    std::unique_ptr<::grpc::ClientAsyncResponseReader<TREPLY>>& responder_reader() {
    	return response_reader_;
    }

private:
    TREPLY                      reply_;
    ClientContext               context_;
    Status                      rpc_status_;
    std::unique_ptr<ClientAsyncResponseReader<TREPLY>> response_reader_;
};


/**
 * A callback interface for handling gRPC response
 *
 *
 */
class CallbackHandler {
public:
	virtual void on_message(ClientCallMethod* cm) = 0;
	virtual ~CallbackHandler() {}
};




/**
 * A gRPC connection, holds a gRPC Service's stub which used to send gRPC request.
 *
 * it implements CallbackHandler interface.
 *
 *
 */
template<typename TSERVICE>
class GrpcConnection : public CallbackHandler {
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
		  completion_queue_(cq)
    {

    }

    ~GrpcConnection() { }

    std::unique_ptr<typename TSERVICE::StubInterface>& stub() {
        return stub_;
    }

    virtual bool init()
    {
        if (!init_channel()) {
            return false;
        }

        init_stub();
        return true;
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

    virtual void init_stub()
    {
        stub_ = TSERVICE::NewStub(channel_);
    }


    virtual bool load_ssl_cert(const std::string& ssl_cert, std::string content)
    {
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


class GrpcConnectionFactory {

public:
    template<typename T>
    static T* Make(const std::string& server_addr, uint32_t dead_line,
            CompletionQueue* cq, const std::string& target_domain,
            const std::string& ssl_cert) {

        T* ret = new T(server_addr, dead_line, cq, target_domain, ssl_cert);
        if (ret->init())
            return ret;

        return nullptr;
    }

};


/**
 * TODO: rename to gRPC client worker -- lhuang8
 * TODO: When work as a async responses handling worker, it's can be hidden from
 *       user of this lib.
 *
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
        for (auto& it : t_) {
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
            t_.push_back(t);
        }

        return true;
    }

    CompletionQueue& cq() { return completion_queue_; }

private:
    void sync_complete_rpc() {  // TODO: looks unuseful, remove it

    }
    void async_complete_rpc() {
        void* got_tag;
        bool ok = false;
        while (completion_queue_.Next(&got_tag, &ok)) {
            if (!ok) {
                continue;
            }

            ClientCallMethod* cm = static_cast<ClientCallMethod*>(got_tag);
            process(cm);
        }
    }

    virtual void process(ClientCallMethod * cm) {
        CallbackHandler* cb = cm->cb_handler();
        cb->on_message(cm);
    }

protected:
    CompletionQueue completion_queue_;

private:
    bool shutdown_;
    std::list<std::shared_ptr<std::thread>> t_;
};




} // end of namespace sds::common::grpc
