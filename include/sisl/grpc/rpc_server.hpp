#pragma once

#include <vector>
#include <memory>
#include <string>

#include <boost/core/noncopyable.hpp>
#include <grpcpp/completion_queue.h>
#include <sisl/logging/logging.h>
#include <sisl/utility/enum.hpp>
#include <sisl/auth_manager/auth_manager.hpp>
#include "rpc_call.hpp"

namespace sisl {
class GenericRpcData;
class GenericRpcStaticInfo;
using generic_rpc_handler_cb_t = std::function< bool(boost::intrusive_ptr< GenericRpcData >&) >;

using rpc_thread_start_cb_t = std::function< void(uint32_t) >;

ENUM(ServerState, uint8_t, VOID, INITED, RUNNING, SHUTTING_DOWN, TERMINATED)

class GrpcServer : private boost::noncopyable {
    friend class RPCHelper;

public:
    GrpcServer(const std::string& listen_addr, uint32_t threads, const std::string& ssl_key,
               const std::string& ssl_cert);
    GrpcServer(const std::string& listen_addr, uint32_t threads, const std::string& ssl_key,
               const std::string& ssl_cert, const std::shared_ptr< sisl::AuthManager >& auth_mgr);
    virtual ~GrpcServer();

    /**
     * Create a new GrpcServer instance and initialize it.
     */
    static GrpcServer* make(const std::string& listen_addr, uint32_t threads = 1, const std::string& ssl_key = "",
                            const std::string& ssl_cert = "");
    static GrpcServer* make(const std::string& listen_addr, const std::shared_ptr< sisl::AuthManager >& auth_mgr,
                            uint32_t threads = 1, const std::string& ssl_key = "", const std::string& ssl_cert = "");

    void run(const rpc_thread_start_cb_t& thread_start_cb = nullptr);
    void shutdown();
    bool is_terminated() const { return m_state.load(std::memory_order_acquire) == ServerState::TERMINATED; }

    template < typename ServiceT >
    bool register_async_service() {
        DEBUG_ASSERT_EQ(ServerState::INITED, m_state, "register service in non-INITED state");

        auto name = ServiceT::service_full_name();
        if (m_services.find(name) != m_services.end()) {
            LOGMSG_ASSERT(false, "Duplicate register async service");
            return false;
        }

        auto svc = new typename ServiceT::AsyncService();
        m_builder.RegisterService(svc);
        m_services.insert({name, svc});

        return true;
    }

    template < typename ServiceT, typename ReqT, typename RespT, bool streaming = false >
    bool register_rpc(const std::string& name, const request_call_cb_t& request_call_cb,
                      const rpc_handler_cb_t& rpc_handler, const rpc_completed_cb_t& done_handler = nullptr) {
        DEBUG_ASSERT_EQ(ServerState::RUNNING, m_state, "register service in non-INITED state");

        auto it = m_services.find(ServiceT::service_full_name());
        if (it == m_services.end()) {
            LOGMSG_ASSERT(false, "RPC registration attempted before service is registered");
            return false;
        }

        auto svc = static_cast< typename ServiceT::AsyncService* >(it->second);

        size_t rpc_idx;
        {
            std::unique_lock lg(m_rpc_registry_mtx);
            rpc_idx = m_rpc_registry.size();
            m_rpc_registry.emplace_back(new RpcStaticInfo< ServiceT, ReqT, RespT, false >(
                this, *svc, request_call_cb, rpc_handler, done_handler, rpc_idx, name));

            // Register one call per cq.
            for (auto i = 0u; i < m_cqs.size(); ++i) {
                auto rpc_call = RpcData< ServiceT, ReqT, RespT, false >::make(
                    (rpc_call_static_info_t*)m_rpc_registry[rpc_idx].get(), i);
                rpc_call->enqueue_call_request(*m_cqs[i]);
            }
        }

        return true;
    }

    template < typename ServiceT, typename ReqT, typename RespT, bool streaming = false >
    bool register_sync_rpc(const std::string& name, const request_call_cb_t& request_call_cb,
                           const rpc_sync_handler_cb_t& handler) {
        return register_rpc(name, request_call_cb, [handler](const RPC_DATA_PTR_SPEC& rpc_data) -> bool {
            rpc_data->set_status(handler(rpc_data->request(), rpc_data->response()));
            return true;
        });
    }

    bool is_auth_enabled() const;
    sisl::AuthVerifyStatus auth_verify(const std::string& token, std::string& msg) const;

    // generic service methods
    bool run_generic_handler_cb(const std::string& rpc_name, boost::intrusive_ptr< GenericRpcData >& rpc_data);
    bool register_async_generic_service();
    bool register_generic_rpc(const std::string& name, const generic_rpc_handler_cb_t& rpc_handler);

private:
    void handle_rpcs(uint32_t thread_num, const rpc_thread_start_cb_t& thread_start_cb);

private:
    std::atomic< ServerState > m_state{ServerState::VOID};
    uint32_t m_num_threads{0};
    ::grpc::ServerBuilder m_builder;

    std::unique_ptr< ::grpc::Server > m_server;
    std::vector< std::shared_ptr< std::thread > > m_threads;
    std::vector< std::unique_ptr< ::grpc::ServerCompletionQueue > > m_cqs;

    std::unordered_map< const char*, ::grpc::Service* > m_services;
    std::mutex m_rpc_registry_mtx;
    std::vector< std::unique_ptr< RpcStaticInfoBase > > m_rpc_registry;
    std::shared_ptr< sisl::AuthManager > m_auth_mgr;
    std::unique_ptr< grpc::AsyncGenericService > m_generic_service;
    std::unique_ptr< GenericRpcStaticInfo > m_generic_rpc_static_info;
    bool m_generic_service_registered{false};
    std::unordered_map< std::string, generic_rpc_handler_cb_t > m_generic_rpc_registry;
    std::shared_mutex m_generic_rpc_registry_mtx;
};
} // namespace sisl::grpc
