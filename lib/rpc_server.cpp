/*
 * server.cpp
 *
 *  Created on: Oct 24, 2018
 */

#include <grpc_helper/rpc_server.hpp>

#ifdef _POSIX_THREADS
#ifndef __APPLE__
extern "C" {
#include <pthread.h>
}
#endif
#endif

#include <grpcpp/impl/codegen/service_type.h>

namespace grpc_helper {

GrpcServer::GrpcServer(const std::string& listen_addr, uint32_t threads, const std::string& ssl_key,
                       const std::string& ssl_cert) :
        m_num_threads{threads} {
    if (listen_addr.empty() || threads == 0) { throw std::invalid_argument("Invalid parameter to start grpc server"); }

#if 0
    if (!ssl_cert.empty() && !ssl_key.empty()) {
        std::string key_contents;
        std::string cert_contents;
        get_file_contents(ssl_cert, cert_contents);
        get_file_contents(ssl_key, key_contents);

        if (cert_contents.empty() || key_contents.empty()) { return false; }

        ::grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp = {key_contents, cert_contents};
        ::grpc::SslServerCredentialsOptions ssl_opts;
        ssl_opts.pem_root_certs = "";
        ssl_opts.pem_key_cert_pairs.push_back(pkcp);

        m_builder.AddListeningPort(listen_addr, ::grpc::SslServerCredentials(ssl_opts));
    } else {
        m_builder.AddListeningPort(listen_addr, ::grpc::InsecureServerCredentials());
    }
#else
    m_builder.AddListeningPort(listen_addr, ::grpc::InsecureServerCredentials());
#endif

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
                             const std::string& ssl_cert) {
    return new GrpcServer(listen_addr, threads, ssl_key, ssl_cert);
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

bool RPCHelper::has_server_shutdown(const GrpcServer* server) {
    return (server->m_state.load(std::memory_order_acquire) != ServerState::RUNNING);
}

} // namespace grpc_helper
