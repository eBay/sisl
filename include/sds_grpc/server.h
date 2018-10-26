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
 * Defines the life cycle of handling a gRPC call.
 *
 */
class BaseServerCallData {
public:
	enum CallStatus { CREATE, PROCESS, FINISH };

    CallStatus& status() { return status_; }

public:

    /**
     * During the life cycle of this object, this method should be called
     * 3 times with different status:
     *  - CREATE is the initial status, the object was just created, it request
     *    that the gRPC server start processing async requests. In this request,
     *    "this" is used as tag for uniquely identifying the request, so that
     *    different CallData instances can serve different requests
     *    concurrently.
     *  - PROCESS is for handling the request, e.g. the incoming request can be
     *    routed to a callback function. Once the handling is done, the gRPC
     *    runtime should be informed, e.g for unary calls,
     *    ServerAsyncResponseWriter<T>::Finish() should be called.
     *  - FINISH is for destroy this object, gRPC server has sent the
     *    appropriate signals to the client to end the call.
     */
    void proceed();

protected:

    BaseServerCallData() : status_(CREATE) {
    }

    virtual ~BaseServerCallData() {}

    /**
     * See BaseServerCallData::proceed() for semantics.
     */
    virtual void do_create() = 0;

    /**
     * See BaseServerCallData::proceed() for semantics.
     */
    virtual void do_process() = 0;

    /**
     * See BaseServerCallData::proceed() for semantics.
     */
    virtual void do_finish();

	CallStatus status_;
};


/**
 * Each instance only handles one request, after that it will be destroyed;
 * a new instance will be created automatically for handling next request.
 *
 */
template<typename TSERVICE, typename TREQUEST, typename TRESPONSE>
class ServerCallData final : public BaseServerCallData {

    typedef std::function<void(TSERVICE*,
                              ::grpc::ServerContext*,
                              TREQUEST*,
                              ::grpc::ServerAsyncResponseWriter<TRESPONSE>*,
                              ::grpc::CompletionQueue*,
                              ::grpc::ServerCompletionQueue*,
                              void *)> request_call_func_t;

    typedef std::function<::grpc::Status(TREQUEST&, TRESPONSE&)> handle_call_func_t;

    typedef ServerCallData<TSERVICE, TREQUEST, TRESPONSE> T;

private:
    template<typename T>
    friend class GrpcServer;

    ServerCallData(TSERVICE * service,
                   ::grpc::ServerCompletionQueue *cq,
                   request_call_func_t wait_request,
                   handle_call_func_t handle_request):
        BaseServerCallData(),
        service_(service), cq_(cq), responder_(&context_),
        wait_request_func_(wait_request),
        handle_request_func_(handle_request) {
    }

    ::grpc::ServerAsyncResponseWriter<TRESPONSE>& responder() { return responder_; }

protected:

    ServerContext context_;

    TSERVICE * service_;
    // The producer-consumer queue where for asynchronous server notifications.
    ::grpc::ServerCompletionQueue* cq_;

    TREQUEST request_;
    TRESPONSE reponse_;
    ::grpc::ServerAsyncResponseWriter<TRESPONSE> responder_;

    request_call_func_t wait_request_func_;
    handle_call_func_t handle_request_func_;

    void do_create()
    {
        wait_request_func_(service_, &context_, &request_, &responder_,
                cq_, cq_, this);
    }

    void do_process()
    {
        (new T(service_, cq_,
               wait_request_func_, handle_request_func_))->proceed();
        //LOGDEBUGMOD(GRPC, "receive {}", request_.GetTypeName());

        ::grpc::Status status = handle_request_func_(request_, reponse_);
        responder_.Finish(reponse_, status, this);
    }

};



template<typename TSERVICE>
class GrpcServer {
public:

    typedef TSERVICE ServiceType;

    GrpcServer();
    virtual ~GrpcServer();

    void shutdown();
    bool is_shutdown();
    bool run(const std::string& ssl_key, const std::string& ssl_cert,
            const std::string& listen_addr, uint32_t threads = 1);

    /**
     * Currently, user need to inherit GrpcServer and register rpc calls.
     * This will be changed by "SDSTOR-464 sds_grpc: make single
     * sds_grpc::GrpcServer instance supports multiple gRPC services"
     */
    virtual void ready() = 0;


    ::grpc::ServerCompletionQueue * completion_queue() {
        return completion_queue_.get();
    }

    template<typename TSVC, typename TREQUEST, typename TRESPONSE>
    void register_rpc(
            std::function<
                void(TSVC*,
                     ::grpc::ServerContext*,
                     TREQUEST*,
                     ::grpc::ServerAsyncResponseWriter<TRESPONSE>*,
                     ::grpc::CompletionQueue*,
                     ::grpc::ServerCompletionQueue*,
                     void *)> request_call_func,
            std::function<::grpc::Status(TREQUEST&, TRESPONSE&)> handle_request_func){

        (new ServerCallData<TSVC, TREQUEST, TRESPONSE> (
             &service_, completion_queue_.get(),
             request_call_func,
             handle_request_func))->proceed();
    }


private:
    // This can be called by multiple threads
    void handle_rpcs();
    void process(BaseServerCallData * cm);

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

        BaseServerCallData* cm = static_cast<BaseServerCallData *>(tag);
        cm->proceed();
    }
}



}
