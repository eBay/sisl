/*
 * Client.cpp
 *
 *  Created on: Sep 19, 2018
 */

#include "sds_grpc/client.h"



namespace sds::grpc {


bool GrpcBaseClient::init() {
    if (!init_channel()) {
        return false;
    }

    return true;
}


bool GrpcBaseClient::init_channel() {

    ::grpc::SslCredentialsOptions ssl_opts;

    if (!ssl_cert_.empty()) {

        if (load_ssl_cert(ssl_cert_, ssl_opts.pem_root_certs)) {
            ::grpc::ChannelArguments channel_args;
            channel_args.SetSslTargetNameOverride(target_domain_);
            channel_ = ::grpc::CreateCustomChannel(server_addr_,
                                                   ::grpc::SslCredentials(ssl_opts),
                                                   channel_args);
        } else {
            return false;
        }
    } else {
        channel_ = ::grpc::CreateChannel(server_addr_,
                                         ::grpc::InsecureChannelCredentials());
    }

    return true;
}

bool GrpcBaseClient::load_ssl_cert(const std::string& ssl_cert, std::string& content) {
    return ::sds::grpc::get_file_contents(ssl_cert, content);;
}


bool GrpcBaseClient::is_connection_ready() {
    return (channel_->GetState(true) ==
            grpc_connectivity_state::GRPC_CHANNEL_READY);
}


std::mutex GrpcAyncClientWorker::mutex_workers;
std::unordered_map<const char *, GrpcAyncClientWorker::UPtr> GrpcAyncClientWorker::workers;

GrpcAyncClientWorker::GrpcAyncClientWorker() {
    state_ = State::INIT;
}


GrpcAyncClientWorker::~GrpcAyncClientWorker() {
    shutdown();
}

void GrpcAyncClientWorker::shutdown() {
    if (state_ == State::RUNNING) {
        completion_queue_.Shutdown();
        state_ = State::SHUTTING_DOWN;

        for (auto& it : threads_) {
            it->join();
        }

        state_ = State::TERMINATED;
    }

    return;
}


bool GrpcAyncClientWorker::run(uint32_t num_threads) {
    BOOST_ASSERT(State::INIT == state_);

    if (num_threads == 0) {
        return false;
    }

    for (uint32_t i = 0; i < num_threads; ++i) {
        std::shared_ptr<std::thread> t = std::shared_ptr<std::thread>(
                                             new std::thread(&GrpcAyncClientWorker::async_complete_rpc, this));
        threads_.push_back(t);
    }

    state_ = State::RUNNING;
    return true;
}


void GrpcAyncClientWorker::async_complete_rpc() {
    void* tag;
    bool ok = false;
    while (completion_queue_.Next(&tag, &ok)) {
        if (!ok) {
            // Client-side StartCallit not going to the wire. This
            // would happen if the channel is either permanently broken or
            // transiently broken but with the fail-fast option.
            continue;
        }

        ClientCallMethod* cm = static_cast<ClientCallMethod*>(tag);
        cm->handle_response();
        delete cm;
    }
}


bool GrpcAyncClientWorker::create_worker(const char * name, int num_thread) {
    std::lock_guard<std::mutex> lock(mutex_workers);

    if (auto it = workers.find(name); it != workers.end()) {
        return true;
    }

    auto worker = std::make_unique<GrpcAyncClientWorker>();
    if (!worker->run(num_thread)) {
        return false;
    }

    workers.insert(std::make_pair(name, std::move(worker)));
    return true;
}


GrpcAyncClientWorker * GrpcAyncClientWorker::get_worker(const char * name) {
    std::lock_guard<std::mutex> lock(mutex_workers);

    auto it = workers.find(name);
    if (it == workers.end()) {
        return nullptr;
    }

    return it->second.get();
}


void GrpcAyncClientWorker::shutdown_all() {
    std::lock_guard<std::mutex> lock(mutex_workers);

    for (auto& it : workers) {
        it.second->shutdown();
        // release worker, the completion queue holds by it need to
        // be destroyed before grpc lib internal object
        // g_core_codegen_interface
        it.second.reset();
    }


}

}





