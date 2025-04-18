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
#include "sisl/grpc/rpc_server.hpp"
#include "sisl/grpc/generic_service.hpp"
#include "utils.hpp"

#ifdef _POSIX_THREADS
#ifndef __APPLE__
extern "C" {
#include <pthread.h>
}
#endif
#endif

#include <grpcpp/impl/codegen/service_type.h>

SISL_LOGGING_DEF(grpc_server)

namespace sisl {
GrpcServer::GrpcServer(const std::string& listen_addr, uint32_t threads, const std::string& ssl_key,
                       const std::string& ssl_cert) :
        GrpcServer::GrpcServer(listen_addr, threads, 0, 0, ssl_key, ssl_cert, nullptr) {}
GrpcServer::GrpcServer(const std::string& listen_addr, uint32_t threads, const std::string& ssl_key,
                       const std::string& ssl_cert, const std::shared_ptr< sisl::GrpcTokenVerifier >& auth_mgr) :
        GrpcServer::GrpcServer(listen_addr, threads, 0, 0, ssl_key, ssl_cert, auth_mgr) {}
GrpcServer::GrpcServer(const std::string& listen_addr, uint32_t threads, int max_receive_msg_size, int max_send_msg_size,
                       const std::string& ssl_key, const std::string& ssl_cert,
                       const std::shared_ptr< sisl::GrpcTokenVerifier >& auth_mgr) :
        m_num_threads{threads}, m_auth_mgr{auth_mgr} {
    if (listen_addr.empty() || threads == 0) { throw std::invalid_argument("Invalid parameter to start grpc server"); }

    if (max_receive_msg_size != 0) {
        LOGINFO("Setting max receive message size to {}", max_receive_msg_size);
        m_builder.SetMaxReceiveMessageSize(max_receive_msg_size);
    }
    if (max_send_msg_size != 0) {
        LOGINFO("Setting max send message size to {}", max_send_msg_size);
        m_builder.SetMaxSendMessageSize(max_send_msg_size);
    }

    if (!ssl_cert.empty() && !ssl_key.empty()) {
        std::string key_contents;
        std::string cert_contents;

        if (!get_file_contents(ssl_cert, cert_contents)) {
            throw std::runtime_error("Unable to load ssl certification for grpc server");
        }
        if (!get_file_contents(ssl_key, key_contents)) {
            throw std::runtime_error("Unable to load ssl key for grpc server");
        }

        ::grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp = {key_contents, cert_contents};
        ::grpc::SslServerCredentialsOptions ssl_opts;
        ssl_opts.pem_root_certs = "";
        ssl_opts.pem_key_cert_pairs.push_back(pkcp);

        m_builder.AddListeningPort(listen_addr, ::grpc::SslServerCredentials(ssl_opts));
    } else {
        m_builder.AddListeningPort(listen_addr, ::grpc::InsecureServerCredentials());
    }

    // Create one cq per thread
    for (auto i = 0u; i < threads; ++i) {
        m_cqs.emplace_back(m_builder.AddCompletionQueue());
    }

    m_state.store(ServerState::INITED);
}

GrpcServer::~GrpcServer() {
    shutdown();
    for (auto& [k, v] : m_services) {
        (void)k;
        delete v;
    }
}

GrpcServer* GrpcServer::make(const std::string& listen_addr, uint32_t threads, const std::string& ssl_key,
                             const std::string& ssl_cert, int max_receive_msg_size, int max_send_msg_size) {
    return GrpcServer::make(listen_addr, nullptr, threads, ssl_key, ssl_cert, max_receive_msg_size, max_send_msg_size);
}

GrpcServer* GrpcServer::make(const std::string& listen_addr, const std::shared_ptr< sisl::GrpcTokenVerifier >& auth_mgr,
                             uint32_t threads, const std::string& ssl_key, const std::string& ssl_cert,
                             int max_receive_msg_size, int max_send_msg_size) {
    return new GrpcServer(listen_addr, threads, max_receive_msg_size, max_send_msg_size, ssl_key, ssl_cert, auth_mgr);
}

void GrpcServer::run(const rpc_thread_start_cb_t& thread_start_cb) {
    LOGMSG_ASSERT_EQ(m_state.load(std::memory_order_relaxed), ServerState::INITED, "Grpcserver duplicate run?");

    m_server = m_builder.BuildAndStart();

    for (uint32_t i = 0; i < m_num_threads; ++i) {
        auto t = std::make_shared< std::thread >(&GrpcServer::handle_rpcs, this, i, thread_start_cb);
#ifdef _POSIX_THREADS
#ifndef __APPLE__
        auto tname = std::string("grpc_server").substr(0, 15);
        pthread_setname_np(t->native_handle(), tname.c_str());
#endif /* __APPLE__ */
#endif /* _POSIX_THREADS */
        m_threads.push_back(t);
    }

    m_state.store(ServerState::RUNNING);
}

void GrpcServer::handle_rpcs(uint32_t thread_num, const rpc_thread_start_cb_t& thread_start_cb) {
    void* tag;
    bool ok = false;

    if (thread_start_cb) { thread_start_cb(thread_num); }

    while (m_cqs[thread_num]->Next(&tag, &ok)) {
        // `ok` is true if read a successful event, false otherwise.
        [[likely]] if (tag != nullptr) {
            // Process the rpc and refill the cq with a new rpc call
            auto new_rpc_call = static_cast< RpcTag* >(tag)->process(ok);
            if (new_rpc_call != nullptr) { new_rpc_call->enqueue_call_request(*m_cqs[new_rpc_call->m_queue_idx]); }
        }
    }
}

void GrpcServer::shutdown() {
    if (m_state.load() == ServerState::RUNNING) {
        m_state.store(ServerState::SHUTTING_DOWN);

        m_server->Shutdown();
        for (auto& cq : m_cqs) {
            cq->Shutdown(); // Always *after* the associated server's Shutdown()!
        }

        m_server->Wait();
        // drain the cq_
        for (auto& thr : m_threads) {
            if (thr->joinable()) thr->join();
        }

        m_state.store(ServerState::TERMINATED);
    }
}

bool GrpcServer::is_auth_enabled() const { return m_auth_mgr != nullptr; }

grpc::Status GrpcServer::auth_verify(grpc::ServerContext const* srv_ctx) const { return m_auth_mgr->verify(srv_ctx); }

bool GrpcServer::run_generic_handler_cb(const std::string& rpc_name, boost::intrusive_ptr< GenericRpcData >& rpc_data) {
    generic_rpc_handler_cb_t cb;
    {
        std::shared_lock< std::shared_mutex > lock(m_generic_rpc_registry_mtx);
        auto it = m_generic_rpc_registry.find(rpc_name);
        if (it == m_generic_rpc_registry.end()) {
            auto status{
                grpc::Status(grpc::StatusCode::UNIMPLEMENTED, fmt::format("generic RPC {} not registered", rpc_name))};
            rpc_data->set_status(status);
            // respond immediately
            return true;
        }
        cb = it->second;
    }
    return cb(rpc_data);
}

bool GrpcServer::register_async_generic_service() {
    if (m_state.load() != ServerState::INITED) {
        LOGMSG_ASSERT(false, "register service in non-INITED state");
        return false;
    }

    if (m_generic_service_registered) {
        LOGWARN("Duplicate register generic async service");
        return false;
    }
    m_generic_service = std::make_unique< grpc::AsyncGenericService >();
    m_builder.RegisterAsyncGenericService(m_generic_service.get());
    m_generic_rpc_static_info = std::make_unique< GenericRpcStaticInfo >(this, m_generic_service.get());
    m_generic_service_registered = true;
    return true;
}

bool GrpcServer::register_generic_rpc(const std::string& name, const generic_rpc_handler_cb_t& rpc_handler) {
    if (m_state.load() != ServerState::RUNNING) {
        LOGMSG_ASSERT(false, "register service in non-INITED state");
        return false;
    }

    if (!m_generic_service_registered) {
        LOGMSG_ASSERT(false, "RPC registration attempted before generic service is registered");
        return false;
    }

    {
        std::unique_lock< std::shared_mutex > lock(m_generic_rpc_registry_mtx);
        if (auto [it, happened]{m_generic_rpc_registry.emplace(name, rpc_handler)}; !happened) {
            LOGWARN("duplicate generic RPC {} registration attempted", name);
            return false;
        }
    }

    // Register one call per cq.
    for (auto i = 0u; i < m_cqs.size(); ++i) {
        auto rpc_call = GenericRpcData::make(m_generic_rpc_static_info.get(), i);
        rpc_call->enqueue_call_request(*m_cqs[i]);
    }
    return true;
}

// RPCHelper static methods

bool RPCHelper::has_server_shutdown(const GrpcServer* server) {
    return (server->m_state.load(std::memory_order_acquire) != ServerState::RUNNING);
}

bool RPCHelper::run_generic_handler_cb(GrpcServer* server, const std::string& method,
                                       boost::intrusive_ptr< GenericRpcData >& rpc_data) {
    return server->run_generic_handler_cb(method, rpc_data);
}

grpc::Status RPCHelper::do_authorization(const GrpcServer* server, const grpc::ServerContext* srv_ctx) {
    return (server->is_auth_enabled()) ? server->auth_verify(srv_ctx) : grpc::Status();
}

} // namespace sisl
