/*
 * server.cpp
 *
 *  Created on: Oct 24, 2018
 */

#include <sds_grpc/server.h>


namespace sds::grpc {

void BaseServerCallData::proceed() {
    if (status_ == CREATE) {
        status_ = PROCESS;
        do_create();
    } else if (status_ == PROCESS) {
        // status must be changed firstly, otherwise this may
        // cause concurrency issue with multi-threads
        status_ = FINISH;
        do_process();
    } else {
        do_finish();
    }
}


void BaseServerCallData::do_finish() {
    GPR_ASSERT(status_ == FINISH);
    // Once in the FINISH state, this can be destroyed
    delete this;
}


GrpcServer::GrpcServer() {

}


GrpcServer::~GrpcServer() {
    shutdown();
}


bool GrpcServer::init(const std::string& listen_addr, uint32_t threads,
                      const std::string& ssl_key, const std::string& ssl_cert) {

    BOOST_ASSERT(State::VOID == state_);

    if (listen_addr.empty() || threads == 0) {
        return false;
    }

    thread_num_ = threads;

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

        builder_.AddListeningPort(listen_addr, ::grpc::SslServerCredentials(ssl_opts));
    } else {
        builder_.AddListeningPort(listen_addr, ::grpc::InsecureServerCredentials());
    }

    cq_ = builder_.AddCompletionQueue();

    state_ = State::INITED;
    return true;
}


GrpcServer* GrpcServer::make(const std::string& listen_addr,
                             uint32_t threads,
                             const std::string& ssl_key,
                             const std::string& ssl_cert) {
    auto ret = new GrpcServer();
    if (!ret->init(listen_addr, threads, ssl_key, ssl_cert)) {
        delete ret;
        return nullptr;
    }

    return ret;
}


bool GrpcServer::run() {

    BOOST_ASSERT(State::INITED == state_);

    server_ = builder_.BuildAndStart();

    for (uint32_t  i = 0; i < thread_num_; ++i) {
        auto t = std::shared_ptr<std::thread>(
                     new std::thread(&GrpcServer::handle_rpcs, this));
        threads_.push_back(t);
    }

    state_ = State::RUNNING;
    return true;
}


void GrpcServer::handle_rpcs() {
    void* tag;
    bool ok = false;

    while (cq_->Next(&tag, &ok)) {
        if (!ok) {
            // the server has been Shutdown before this particular
            // call got matched to an incoming RPC.
            continue;
        }

        BaseServerCallData* cm = static_cast<BaseServerCallData *>(tag);
        cm->proceed();
    }
}

void GrpcServer::shutdown() {
    if (state_ == State::RUNNING) {
        server_->Shutdown();
        cq_->Shutdown(); // Always *after* the associated server's Shutdown()!
        state_ = State::SHUTTING_DOWN;

        // drain the cq_
        for (auto& it : threads_) {
            it->join();
        }

        state_ = State::TERMINATED;
    }

    return;
}



}
