/*
 * Server.h
 *
 *  Created on: Sep 19, 2018
 */


#pragma once

#include <fstream>

#include <grpc++/grpc++.h>
#include <grpc/support/log.h>
#include <sds_logging/logging.h>

#include <functional>


namespace sds::grpc
{

using ::grpc::Server;
using ::grpc::ServerAsyncResponseWriter;
using ::grpc::ServerBuilder;
using ::grpc::ServerContext;
using ::grpc::ServerCompletionQueue;
using ::grpc::Status;



/**
 * ServerCallMethod : Stores the incoming request callback handler and method name of the rpc
 *
 * TODO: rename as BaseServerCallData
 */
class ServerCallMethod {
public:
	enum CallStatus { CREATE, PROCESS, FINISH };

public:
	ServerCallMethod(const std::string& method_name):
		method_name_(method_name), status_(CREATE) {
	}

	virtual ~ServerCallMethod(){}

	const std::string& method_name() { return method_name_; }
	CallStatus& status() { return status_; }

    void proceed() {
        if (status_ == CREATE){

            do_create();
            status_ = PROCESS;

        } else if (status_ == PROCESS) {

            do_process();
            status_ = FINISH;
        } else {
            do_finish();
        }
    }


protected:

    virtual void do_create() = 0;
    virtual void do_process() = 0;

    virtual void do_finish(){
        GPR_ASSERT(status_ == FINISH);
        // Once in the FINISH state, deallocate ourselves
        delete this;
    }


	std::string         method_name_; // TODO: looks like not useful -- lhuang8
	CallStatus          status_;

};


/**
 * Once a new instance's proceed() method being called, it will begin to wait for
 * one request.
 *
 * Each instance only handles one request, after that it will be  destroyed;
 * a new instance will be created automatically for handling next request.
 *
 */
template<typename TSERVICE, typename TREQUEST, typename TREPLY>
class ServerCallData final : public ServerCallMethod {


    typedef std::function<void(TSERVICE*,
                ::grpc::ServerContext*, TREQUEST*,
                ::grpc::ServerAsyncResponseWriter<TREPLY>*,
                ::grpc::CompletionQueue*, ::grpc::ServerCompletionQueue*,
                 void *)> wait_request_cb_t;

    typedef std::function<TREPLY(TREQUEST&)> handle_request_cb_t;

    typedef ServerCallData<TSERVICE, TREQUEST, TREPLY> T;

public:
    ServerCallData(TSERVICE * service,
                   ::grpc::ServerCompletionQueue *cq,
                   const std::string& method_name,
                   wait_request_cb_t wait_request,
                   handle_request_cb_t handle_request):
        ServerCallMethod(method_name),
        service_(service), cq_(cq), responder_(&context_),
        wait_request_cb_(wait_request), handle_request_cb_(handle_request) {

    }

    ServerContext& context() { return context_; }
    TREQUEST& request() { return request_; }
    TREPLY& reply() { return reply_; }
    ::grpc::ServerAsyncResponseWriter<TREPLY>& responder() { return responder_; }

protected:


    ServerContext   context_;

    TSERVICE *      service_;
    // The producer-consumer queue where for asynchronous server notifications.
    ::grpc::ServerCompletionQueue* cq_;

    TREQUEST        request_;
    TREPLY          reply_;
    ::grpc::ServerAsyncResponseWriter<TREPLY> responder_;

    wait_request_cb_t wait_request_cb_;
    handle_request_cb_t handle_request_cb_;



    void do_create()
    {
        wait_request_cb_(service_, &context_, &request_, &responder_,
                cq_, cq_, this);
    }

    void do_process()
    {
        (new T(service_, cq_, method_name_,
                wait_request_cb_, handle_request_cb_))->proceed();
        //LOGDEBUGMOD(GRPC, "receive {}", request_.GetTypeName());

        reply_ = handle_request_cb_(request_);
        responder_.Finish(reply_, Status::OK, this);
    }


};



template<typename TSERVICE>
class GrpcServer {
public:
    GrpcServer();
    virtual ~GrpcServer();

    void shutdown();
    bool is_shutdown();
    bool run(const std::string& ssl_key, const std::string& ssl_cert,
            const std::string& listen_addr, uint32_t threads = 1);

    virtual void ready() = 0;
    virtual void process(ServerCallMethod * cm) = 0;


    ::grpc::ServerCompletionQueue * completion_queue() {
        return completion_queue_.get();
    }

private:
    // This can be run in multiple threads if needed.
    void handle_rpcs();
    // TODO: move this function to utils
    bool get_file_contents(const std::string& file_name, std::string& contents);

protected:
    std::unique_ptr<::grpc::ServerCompletionQueue> completion_queue_;
    std::unique_ptr<Server>     server_;
    TSERVICE                    service_;

private:
    bool shutdown_;
    std::list<std::shared_ptr<std::thread>> threads_;
};


template<typename TSERVICE>
GrpcServer<TSERVICE>::GrpcServer()
    :shutdown_(true)
{}


template<typename TSERVICE>
GrpcServer<TSERVICE>::~GrpcServer() {
    shutdown();
    for (auto& it : threads_) {
        it->join();
    }
}


template<typename TSERVICE>
void GrpcServer<TSERVICE>::shutdown() {
    if (!shutdown_) {
        server_->Shutdown();
        completion_queue_->Shutdown();
        shutdown_ = true;

    }
}

template<typename TSERVICE>
bool GrpcServer<TSERVICE>::is_shutdown() {
    return shutdown_;
}


template<typename TSERVICE>
bool GrpcServer<TSERVICE>::run(const std::string& ssl_key, const std::string& ssl_cert,
        const std::string& listen_addr, uint32_t threads /* = 1 */) {
    if (listen_addr.empty() || threads == 0) {
        return false;
    }

    ServerBuilder builder;
    if (!ssl_cert.empty() && !ssl_key.empty()) {
        std::string     key_contents;
        std::string     cert_contents;
        get_file_contents(ssl_cert, cert_contents);
        get_file_contents(ssl_key, key_contents);

        if (cert_contents.empty() || key_contents.empty()) {
            return false;
        }

        ::grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp = { key_contents, cert_contents };
        ::grpc::SslServerCredentialsOptions ssl_opts;
        ssl_opts.pem_root_certs = "";
        ssl_opts.pem_key_cert_pairs.push_back(pkcp);

        builder.AddListeningPort(listen_addr, ::grpc::SslServerCredentials(ssl_opts));
    } else {
        builder.AddListeningPort(listen_addr, ::grpc::InsecureServerCredentials());
    }

    builder.RegisterService(&service_);
    completion_queue_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
    //LOGDEBUGMOD(GRPC, "Server listening on {}", listen_addr);

    shutdown_ = false;
    ready();

    for (uint32_t  i = 0; i < threads; ++i) {
        std::shared_ptr<std::thread> t =
            std::shared_ptr<std::thread>(new std::thread(&GrpcServer<TSERVICE>::handle_rpcs, this));
        threads_.push_back(t);
    }

    return true;
}


template<typename TSERVICE>
bool GrpcServer<TSERVICE>::get_file_contents(const std::string& file_name, std::string& contents) {
    try {
        std::ifstream in(file_name.c_str(), std::ios::in);
        if (in) {
            std::ostringstream t;
            t << in.rdbuf();
            in.close();

            contents = t.str();
            return true;
        }
    } catch (...) {

    }

    return false;
}

template<typename TSERVICE>
void GrpcServer<TSERVICE>::handle_rpcs() {
    void* tag;
    bool ok = false;

    while (completion_queue_->Next(&tag, &ok)) {
        if (!ok) {
            // the server has been Shutdown before this particular
            // call got matched to an incoming RPC.
            continue;
        }

        ServerCallMethod* cm = static_cast<ServerCallMethod *>(tag);
        process(cm);
    }
}



}
