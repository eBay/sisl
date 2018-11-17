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
#include <unordered_map>
#include <boost/core/noncopyable.hpp>
#include <boost/assert.hpp>
#include "utils.h"


namespace sds::grpc {

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

    CallStatus& status() {
        return status_;
    }

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

    using request_call_func_t = std::function<
                                void(TSERVICE*,
                                     ::grpc::ServerContext*,
                                     TREQUEST*,
                                     ::grpc::ServerAsyncResponseWriter<TRESPONSE>*,
                                     ::grpc::CompletionQueue*,
                                     ::grpc::ServerCompletionQueue*,
                                     void *)>;

    using handle_call_func_t = std::function<
                               ::grpc::Status(TREQUEST&, TRESPONSE&)>;

    using T = ServerCallData<TSERVICE, TREQUEST, TRESPONSE>;

  private:
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

    ::grpc::ServerAsyncResponseWriter<TRESPONSE>& responder() {
        return responder_;
    }

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

    void do_create() {
        wait_request_func_(service_, &context_, &request_, &responder_,
                           cq_, cq_, this);
    }

    void do_process() {
        (new T(service_, cq_,
               wait_request_func_, handle_request_func_))->proceed();
        //LOGDEBUGMOD(GRPC, "receive {}", request_.GetTypeName());

        ::grpc::Status status = handle_request_func_(request_, reponse_);
        responder_.Finish(reponse_, status, this);
    }

};



class GrpcServer : private boost::noncopyable {

    enum State {
        VOID,
        INITED,
        RUNNING,
        SHUTTING_DOWN,
        TERMINATED
    };

  private:
    GrpcServer();

    bool init(const std::string& listen_addr, uint32_t threads,
              const std::string& ssl_key, const std::string& ssl_cert);

  public:
    virtual ~GrpcServer();

    /**
     * Create a new GrpcServer instance and initialize it.
     */
    static GrpcServer* make(const std::string& listen_addr,
                            uint32_t threads=1,
                            const std::string& ssl_key="",
                            const std::string& ssl_cert="");

    bool run();

    void shutdown();

    bool is_terminated() {
        return state_ == State::TERMINATED;
    }

    ::grpc::ServerCompletionQueue * completion_queue() {
        return cq_.get();
    }

    template<typename TSVC>
    bool register_async_service() {

        BOOST_ASSERT_MSG(State::INITED == state_,
                         "register service in non-INITED state");

        auto name = TSVC::service_full_name();

        BOOST_ASSERT_MSG(services_.find(name) == services_.end(),
                         "Double register async service.");
        if (services_.find(name) != services_.end()) {
            return false;
        }

        auto svc = new typename TSVC::AsyncService();
        builder_.RegisterService(svc);
        services_.insert({name, svc});

        return true;
    }

    template<typename TSVC, typename TREQUEST, typename TRESPONSE>
    bool register_rpc(
        std::function<
        void(typename TSVC::AsyncService*,
             ::grpc::ServerContext*,
             TREQUEST*,
             ::grpc::ServerAsyncResponseWriter<TRESPONSE>*,
             ::grpc::CompletionQueue*,
             ::grpc::ServerCompletionQueue*,
             void *)> request_call_func,
        std::function<::grpc::Status(TREQUEST&, TRESPONSE&)> handle_request_func) {

        BOOST_ASSERT_MSG(State::RUNNING == state_,
                         "register service in non-INITED state");

        auto it = services_.find(TSVC::service_full_name());
        if (it == services_.end()) {
            BOOST_ASSERT_MSG(false, "service not registered");
            return false;
        }

        auto svc = static_cast<typename TSVC::AsyncService*>(it->second);
        (new ServerCallData<typename TSVC::AsyncService, TREQUEST, TRESPONSE> (
             svc, cq_.get(),
             request_call_func,
             handle_request_func))->proceed();

        return true;
    }


  private:

    /*
     * This can be called by multiple threads
     */
    void handle_rpcs();

    void process(BaseServerCallData * cm);

    State state_ = State::VOID;

    uint32_t thread_num_ = 0;

    ServerBuilder builder_;

    std::unique_ptr<::grpc::ServerCompletionQueue> cq_;
    std::unique_ptr<Server> server_;
    std::list<std::shared_ptr<std::thread>> threads_;

    std::unordered_map<const char *, ::grpc::Service *> services_;
};


}
