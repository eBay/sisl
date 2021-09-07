#pragma once

#include <atomic>
#include <queue>
#include <mutex>

#include <grpcpp/grpcpp.h>
#include <google/protobuf/arena.h>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

#include <sds_logging/logging.h>
#include <sisl/utility/obj_life_counter.hpp>
#include <sisl/utility/atomic_counter.hpp>
#include <sisl/utility/enum.hpp>
#include "rpc_common.hpp"

SDS_LOGGING_DECL(grpc_server)

#define RPC_SERVER_LOG(level, msg, ...)                                                                                \
    LOG##level##MOD_FMT(grpc_server, ([&](fmt::memory_buffer& buf, const char* __m, auto&&... args) -> bool {          \
                            fmt::format_to(buf, "[{}:{}] [RPC={} id={}] ", file_name(__FILE__), __LINE__,              \
                                           m_rpc_info->m_rpc_name, request_id());                                      \
                            fmt::format_to(buf, __m, std::forward< decltype(args) >(args)...);                         \
                            return true;                                                                               \
                        }),                                                                                            \
                        msg, ##__VA_ARGS__);

namespace grpc_helper {
class RpcDataAbstract : public boost::intrusive_ref_counter< RpcDataAbstract, boost::thread_safe_counter > {
public:
    RpcDataAbstract(size_t queue_idx) :
            m_queue_idx{queue_idx}, m_request_id(s_glob_request_id.fetch_add(1, std::memory_order_relaxed)) {}

    virtual ~RpcDataAbstract() = default;
    virtual size_t get_rpc_idx() const = 0;

    ::grpc::ServerContext& server_context() noexcept { return m_ctx; }
    uint64_t request_id() const { return m_request_id; }
    bool canceled() const { return m_is_canceled; }

    // enqueues this call to be matched with incoming rpc requests
    virtual void enqueue_call_request(::grpc::ServerCompletionQueue& cq) = 0;

    // the grpc queue index on which this request is to be enqueued
    size_t const m_queue_idx;

protected:
    // ref counter of this instance
    RpcDataAbstract* ref() {
        intrusive_ptr_add_ref(this);
        return this;
    }
    void unref() { intrusive_ptr_release(this); }
    virtual RpcDataAbstract* create_new() = 0;
    friend class RpcTag;

    uint64_t const m_request_id;
    grpc::ServerContext m_ctx;
    std::atomic_bool m_is_canceled{false};
    static inline std::atomic< uint64_t > s_glob_request_id = 0;
};

// Associates a tag in a `::grpc::CompletionQueue` with a callback
// for an incoming RPC.  An active Tag owns a reference on the corresponding
// RpcData object.
class RpcTag {
public:
    RpcTag(RpcDataAbstract* rpc) : m_rpc_data{rpc} {}
    RpcTag* ref() {
        m_rpc_data->ref();
        return this;
    }
    // Calls the callback associated with this tag.
    // The callback takes ownership of `this->call_`.
    // @return if not null - a replacement of this call for registration with the server; null otherwise
    RpcDataAbstract* process(bool ok) {
        RpcDataAbstract* ret = do_process(ok);
        m_rpc_data->unref(); // undo ref() acquired when tag handed over to grpc.
        return ret;
    }

protected:
    virtual RpcDataAbstract* do_process(bool ok) = 0;
    RpcDataAbstract* const m_rpc_data; // `this` owns one reference.
};

class RpcStaticInfoBase {
public:
    virtual ~RpcStaticInfoBase() = default;
};

template < typename ServiceT, typename ReqT, typename RespT, bool streaming >
class RpcData;

template < typename ServiceT, typename ReqT, typename RespT >
using AsyncRpcDataPtr = boost::intrusive_ptr< RpcData< ServiceT, ReqT, RespT, false > >;

template < typename ServiceT, typename ReqT, typename RespT >
using StreamRpcDataPtr = boost::intrusive_ptr< RpcData< ServiceT, ReqT, RespT, true > >;

#define RPC_DATA_PTR_SPEC boost::intrusive_ptr< RpcData< ServiceT, ReqT, RespT, streaming > >
#define request_call_cb_t                                                                                              \
    std::function< void(typename ServiceT::AsyncService*, ::grpc::ServerContext*, ReqT*,                               \
                        ::grpc::ServerAsyncResponseWriter< RespT >*, ::grpc::CompletionQueue*,                         \
                        ::grpc::ServerCompletionQueue*, void*) >
#define rpc_handler_cb_t std::function< bool(const RPC_DATA_PTR_SPEC& rpc_call) >
#define rpc_completed_cb_t std::function< void(const RPC_DATA_PTR_SPEC& rpc_call) >
#define rpc_call_static_info_t RpcStaticInfo< ServiceT, ReqT, RespT, streaming >
#define rpc_sync_handler_cb_t std::function< ::grpc::Status(const ReqT&, RespT&) >

// This class represents all static information related to a specific RpcData, so these information does not need to be
// built for every RPC
template < typename ServiceT, typename ReqT, typename RespT, bool streaming = false >
class RpcStaticInfo : public RpcStaticInfoBase {
public:
    RpcStaticInfo(GrpcServer* server, typename ServiceT::AsyncService& svc, const request_call_cb_t& call_cb,
                  const rpc_handler_cb_t& rpc_cb, const rpc_completed_cb_t& comp_cb, size_t idx,
                  const std::string& name) :
            m_server{server},
            m_svc{svc},
            m_req_call_cb{call_cb},
            m_handler_cb{rpc_cb},
            m_comp_cb{comp_cb},
            m_rpc_idx{idx},
            m_rpc_name{name} {}

    GrpcServer* m_server;
    typename ServiceT::AsyncService& m_svc;
    request_call_cb_t m_req_call_cb;
    rpc_handler_cb_t m_handler_cb;
    rpc_completed_cb_t m_comp_cb;
    size_t m_rpc_idx;
    std::string m_rpc_name;
};

/**
 * This class represents an incoming request and its associated context
 * Template argument 'streaming' should be understood as server streaming. If we later want
 * client/bidirectional streaming then we can restructure this code
 */
template < typename ServiceT, typename ReqT, typename RespT, bool streaming = false >
class RpcData : public RpcDataAbstract, sisl::ObjLifeCounter< RpcData< ServiceT, ReqT, RespT, streaming > > {
public:
    static RpcDataAbstract* make(rpc_call_static_info_t* rpc_info, size_t queue_idx) {
        return new RpcData(rpc_info, queue_idx);
    }

    RpcDataAbstract* create_new() override { return new RpcData(m_rpc_info, m_queue_idx); }
    ~RpcData() override = default;

    const ReqT& request() const { return *m_request; }

    template < bool mode = streaming >
    std::enable_if_t< !mode, RespT& > response() {
        return *m_response;
    }

    void set_status(grpc::Status status) { m_retstatus = status; }

    // invoked by the application completion flow when the response payload `m_response` is formed
    //@param is_last - true to indicate that this is the last chunk in a streaming response (where
    // applicable)
    // NOTE: this function MUST `unref()` this call
    template < bool mode = streaming >
    std::enable_if_t< !mode, void > send_response(bool is_last = true) {
        do_non_streaming_send();
    }

    /**
     * @param response, the response should own by m_arena_resp
     * @param is_last
     * @return return false, when we can't send send_response anymore.
     * The reasons includes:
     *  1. last streaming response has sent
     *
     *  2. the rpc call has canceled.
     *  3. ok == false in ResponseSent
     *  4. e.t.c
     *  Note: We must call send_response with is_last = true once even when the call return false at
     * last time to indicate use will not hold the RpcData anymore.
     */
    template < bool mode = streaming >
    std::enable_if_t< mode, bool > send_response(std::unique_ptr< RespT > response, bool is_last) {
        std::lock_guard< std::mutex > lock{m_streaming_mutex};
        if (is_last && !m_last) {
            m_last = true;
            // ses comment in _start_request_processing
            unref();
        }
        if (m_streaming_disable_enqueue) { return false; }
        if (m_last) { m_streaming_disable_enqueue = true; }

        RPC_SERVER_LOG(DEBUG, "ENQUEUE STREAMING RESPONSE, is_last={}", is_last);
        RPC_SERVER_LOG(TRACE, "resp. payload={}", response->DebugString());

        m_pending_streaming_responses.push(std::move(response));
        do_streaming_send_if_needed();
        return !m_streaming_disable_enqueue;
    }

    ::grpc::string get_peer_info() { return m_ctx.peer(); }
    std::string get_client_req_context() {
        /*if (m_client_req_context.empty()) {
            std::string* client_id_str = google::protobuf::Arena::Create< std::string >(
                &m_arena_req, m_ctx.peer() + "_" + std::to_string(request_id()));
            m_client_req_context = grpc::string_ref(*client_id_str);
        }
        return m_client_req_context; */
        return fmt::format("{}_{}", m_ctx.peer(), request_id());
    }
    size_t get_rpc_idx() const { return m_rpc_info->m_rpc_idx; }

    RpcData(rpc_call_static_info_t* rpc_info, size_t queue_idx) :
            RpcDataAbstract{queue_idx},
            m_rpc_info{rpc_info},
            m_request{google::protobuf::Arena::CreateMessage< ReqT >(&m_arena_req)},
            m_response{google::protobuf::Arena::CreateMessage< RespT >(&m_arena_resp)},
            // m_rpc_context{google::protobuf::Arena::Create< context_t >(&m_arena_req, *this)},
            m_responder(&m_ctx),
            m_streaming_responder(&m_ctx) {}

private:
    // The implementation of this method should dispatch the request for processing by calling
    // do_start_request_processing One reference on `this` is transferred to the callee, and the
    // callee is responsible for releasing it (typically via `RpcData::send_response(..)`).
    //
    // `ok` is true if the request was received is a "regular event", otherwise false.
    // @return a new instance of the same class for enqueueing as a replacement of this call
    RpcDataAbstract* on_request_received(bool ok) {
        bool in_shutdown = RPCHelper::has_server_shutdown(m_rpc_info->m_server);
        RPC_SERVER_LOG(TRACE, "request received with is_ok={} is_shutdown={}", ok, in_shutdown);

        if (ok) {
            ref(); // we now own one ref since we are starting the processing

            RPC_SERVER_LOG(DEBUG, "Received client_req_context={}, from peer={}", get_client_req_context(),
                           get_peer_info());
            RPC_SERVER_LOG(TRACE, "req. payload={}", request().DebugString());

            if constexpr (streaming) {
                // In no-streaming mode, we call ref() to inc the ref count for keep the RpcData live
                // before users finish their work and send responses in RequestReceived.
                // But in streaming mode, The time user finishes their work may be different to
                // the time grpc finsihes the grpc call. E.g.:
                // 1) The user queues the last streaming resposne. At that time. We can't unref the RpcData and
                // must do it after it sends all responses.
                // 2) The user queues a no-last streaming response, then RpcData find the call was canceled.
                // We can't unref the call, because users don't know it, they will send next responses.
                // So instead of using only one ref in no-streaming mode. We use two ref to make lifecyle clear:
                // 1) first one in RequestReceived and unref after grpc call finished.
                // 2) second one in here and unref after called send_response with is_last = true;
                ref();
            }
            if (m_rpc_info->m_handler_cb(RPC_DATA_PTR_SPEC{this})) { send_response(); }
        }

        return in_shutdown ? nullptr : create_new();
    }

    // This method will be called in response to one of `m_responder.Finish*` flavours
    RpcDataAbstract* on_response_sent(bool ok) {
        RPC_SERVER_LOG(TRACE, "response sent with is_ok={}", ok);

        if constexpr (streaming) {
            if (ok) {
                std::lock_guard< std::mutex > lock{m_streaming_mutex};
                m_write_pending = false;
                do_streaming_send_if_needed();
            } else {
                m_streaming_disable_enqueue = true;
                // The ResponseSent can be triggered by Write, WriteAndFinish and Finish.
                // Only when it triggered by Write, we should call unref()
                if (!m_streaming_disable_send) { unref(); }
            }
        }
        return nullptr;
    }

    // This method will be called either (i) when the server is notified that the request has been canceled, or (ii)
    // when the request completes normally. The implementation should distinguish these cases by querying the
    // grpc::ServerContext associated with the request.
    RpcDataAbstract* on_request_completed(bool ok) {
        RPC_SERVER_LOG(TRACE, "request completed with is_ok={}", ok);
        if (m_ctx.IsCancelled()) {
            m_is_canceled.store(true, std::memory_order_release);
            RPC_SERVER_LOG(DEBUG, "request is CANCELLED by the caller");
        }
        if (m_rpc_info->m_comp_cb) { m_rpc_info->m_comp_cb(RPC_DATA_PTR_SPEC{this}); }
        return nullptr;
    }

    void enqueue_call_request(::grpc::ServerCompletionQueue& cq) override {
        RPC_SERVER_LOG(TRACE, "enqueue new call request");

        if (m_rpc_info->m_comp_cb) {
            // Creates a completion queue tag for handling cancellation by the client.
            // NOTE: This method must be called before this call is enqueued on a completion queue.
            m_ctx.AsyncNotifyWhenDone(m_completed_tag.ref());
        }

        m_rpc_info->m_req_call_cb(&m_rpc_info->m_svc, &m_ctx, m_request, &m_responder, &cq, &cq,
                                  m_request_received_tag.ref());
    }

    // actual sending of the response via grpc
    // MUST unref() after send is enqueued
    void do_non_streaming_send() {
        if (!m_is_canceled.load(std::memory_order_relaxed)) {
            RPC_SERVER_LOG(DEBUG, "SENDING RESPONSE");
            RPC_SERVER_LOG(TRACE, "resp. payload={}", m_response->DebugString());

            if (m_retstatus.ok()) {
                m_responder.Finish(*m_response, grpc::Status::OK, m_response_sent_tag.ref());
            } else {
                m_responder.FinishWithError(m_retstatus, m_response_sent_tag.ref());
            }
        }
        unref(); // because we have enqueued response for this call and not longer own it
    }

    // MUST be called in streaming mode and under m_streaming_mutex.
    void do_streaming_send_if_needed() {
        if (m_streaming_disable_send) { return; }

        if (m_is_canceled.load(std::memory_order_relaxed)) {
            m_streaming_disable_enqueue = true;
            m_streaming_disable_send = true;
            unref();
            return;
        }

        if (m_write_pending) { return; }

        if (!m_retstatus.ok()) {
            m_streaming_responder.Finish(m_retstatus, m_response_sent_tag.ref());
            m_streaming_disable_enqueue = true;
            m_streaming_disable_send = true;
            unref();
            return;
        }

        if (m_pending_streaming_responses.empty()) { return; }
        auto response = std::move(m_pending_streaming_responses.front());
        m_pending_streaming_responses.pop();
        if (m_pending_streaming_responses.empty() && m_streaming_disable_enqueue) {
            RPC_SERVER_LOG(DEBUG, "SENDING LAST STREAMING RESPONSE");
            RPC_SERVER_LOG(TRACE, "resp. payload={}", m_response->DebugString());

            m_streaming_responder.WriteAndFinish(*response, grpc::WriteOptions(), grpc::Status::OK,
                                                 m_response_sent_tag.ref());
            m_streaming_disable_send = true;
            unref();
        } else {
            RPC_SERVER_LOG(DEBUG, "SENDING STREAMING RESPONSE");
            RPC_SERVER_LOG(TRACE, "resp. payload={}", m_response->DebugString());
            m_streaming_responder.Write(*response, grpc::WriteOptions(), m_response_sent_tag.ref());
            m_write_pending = true;
        }
    }

private:
    rpc_call_static_info_t* m_rpc_info;
    ::google::protobuf::Arena m_arena_req, m_arena_resp;
    ReqT* const m_request;
    RespT* const m_response;

    // this field is used when there is a high level grpc-level request error
    grpc::Status m_retstatus{grpc::Status::OK};

    grpc::ServerAsyncResponseWriter< RespT > m_responder;
    grpc::ServerAsyncWriter< RespT > m_streaming_responder;

    std::mutex m_streaming_mutex;
    bool m_last{false};
    bool m_write_pending{false};
    bool m_streaming_disable_enqueue{false};
    bool m_streaming_disable_send{false};
    std::queue< std::unique_ptr< RespT > > m_pending_streaming_responses;

    // implements abstract method `_process() by delegating to registered pointer to member function
    struct RpcTagImpl : public RpcTag {
        using callback_type = RpcDataAbstract* (RpcData::*)(bool ok);
        RpcTagImpl(RpcData* rpc, callback_type cb) : RpcTag{rpc}, m_callback{cb} {}

        RpcDataAbstract* do_process(bool ok) override { return (static_cast< RpcData* >(m_rpc_data)->*m_callback)(ok); }

        callback_type m_callback;
    };

    // Used as void* completion markers from grpc to indicate different events of interest for a
    // Call.
    RpcTagImpl m_request_received_tag{this, &RpcData::on_request_received};
    RpcTagImpl m_response_sent_tag{this, &RpcData::on_response_sent};
    RpcTagImpl m_completed_tag{this, &RpcData::on_request_completed};
};

} // namespace grpc_helper
