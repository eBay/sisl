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
#include "sisl/grpc/rpc_client.hpp"
#include "utils.hpp"

SISL_LOGGING_DECL(grpc_server)

namespace sisl {

GrpcBaseClient::GrpcBaseClient(const std::string& server_addr, const std::string& target_domain,
                               const std::string& ssl_cert) :
        GrpcBaseClient::GrpcBaseClient(server_addr, nullptr, target_domain, ssl_cert, 0, 0) {}

GrpcBaseClient::GrpcBaseClient(const std::string& server_addr,
                               const std::shared_ptr< sisl::GrpcTokenClient >& token_client,
                               const std::string& target_domain, const std::string& ssl_cert,
                               const int max_receive_msg_size, const int max_send_msg_size) :
        m_server_addr(server_addr),
        m_target_domain(target_domain),
        m_ssl_cert(ssl_cert),
        m_token_client(token_client),
        m_max_receive_msg_size(max_receive_msg_size),
        m_max_send_msg_size(max_send_msg_size) {}

void GrpcBaseClient::init() {
    ::grpc::SslCredentialsOptions ssl_opts;
    ::grpc::ChannelArguments channel_args;
    channel_args.SetMaxReceiveMessageSize(-1);
    if (m_max_receive_msg_size != 0) {
        LOGINFO("Setting max receive message size to {}", m_max_receive_msg_size);
        channel_args.SetMaxReceiveMessageSize(m_max_receive_msg_size);
    }
    if (m_max_send_msg_size != 0) {
        LOGINFO("Setting max send message size to {}", m_max_send_msg_size);
        channel_args.SetMaxSendMessageSize(m_max_send_msg_size);
    }

    if (!m_ssl_cert.empty()) {
        if (load_ssl_cert(m_ssl_cert, ssl_opts.pem_root_certs)) {
            if (!m_target_domain.empty()) { channel_args.SetSslTargetNameOverride(m_target_domain); }
            m_channel = ::grpc::CreateCustomChannel(m_server_addr, ::grpc::SslCredentials(ssl_opts), channel_args);
        } else {
            throw std::runtime_error("Unable to load ssl certification for grpc client");
        }
    } else {
        m_channel = ::grpc::CreateCustomChannel(m_server_addr, ::grpc::InsecureChannelCredentials(), channel_args);
    }
}

bool GrpcBaseClient::load_ssl_cert(const std::string& ssl_cert, std::string& content) {
    return get_file_contents(ssl_cert, content);
}

bool GrpcBaseClient::is_connection_ready() const {
    return (m_channel->GetState(true) == grpc_connectivity_state::GRPC_CHANNEL_READY);
}

std::mutex GrpcAsyncClientWorker::s_workers_mtx;
std::unordered_map< std::string, GrpcAsyncClientWorker::UPtr > GrpcAsyncClientWorker::s_workers;

GrpcAsyncClientWorker::~GrpcAsyncClientWorker() { shutdown(); }
void GrpcAsyncClientWorker::shutdown() {
    if (m_state == ClientState::RUNNING) {
        m_cq.Shutdown();
        m_state = ClientState::SHUTTING_DOWN;

        for (auto& thr : m_threads) {
            thr.join();
        }

        m_state = ClientState::TERMINATED;
    }

    return;
}

void GrpcAsyncClientWorker::run(uint32_t num_threads) {
    LOGMSG_ASSERT_EQ(ClientState::INIT, m_state);

    if (num_threads == 0) { throw(std::invalid_argument("Need atleast one worker thread")); }
    for (uint32_t i = 0u; i < num_threads; ++i) {
        m_threads.emplace_back(&GrpcAsyncClientWorker::client_loop, this);
    }

    m_state = ClientState::RUNNING;
}

void GrpcAsyncClientWorker::client_loop() {
#ifdef _POSIX_THREADS
#ifndef __APPLE__
    auto tname = std::string("grpc_client").substr(0, 15);
    pthread_setname_np(pthread_self(), tname.c_str());
#endif /* __APPLE__ */
#endif /* _POSIX_THREADS */

    void* tag;
    bool ok = false;
    while (m_cq.Next(&tag, &ok)) {
        // For client-side unary call, `ok` is always true, even server is not running
        auto cm = static_cast< ClientRpcDataAbstract* >(tag);
        cm->handle_response(ok);
        delete cm;
    }
}

void GrpcAsyncClientWorker::create_worker(const std::string& name, int num_threads) {
    std::lock_guard< std::mutex > lock(s_workers_mtx);
    if (s_workers.find(name) != s_workers.end()) { return; }

    auto worker = std::make_unique< GrpcAsyncClientWorker >();
    worker->run(num_threads);
    s_workers.insert(std::make_pair(name, std::move(worker)));
}

GrpcAsyncClientWorker* GrpcAsyncClientWorker::get_worker(const std::string& name) {
    std::lock_guard< std::mutex > lock(s_workers_mtx);
    auto it = s_workers.find(name);
    if (it == s_workers.end()) { return nullptr; }
    return it->second.get();
}

void GrpcAsyncClientWorker::shutdown_all() {
    std::lock_guard< std::mutex > lock(s_workers_mtx);
    for (auto& it : s_workers) {
        it.second->shutdown();
        // release worker, the completion queue holds by it need to  be destroyed before grpc lib internal object
        // g_core_codegen_interface
        it.second.reset();
    }
    s_workers.clear();
}

void GrpcAsyncClient::GenericAsyncStub::prepare_and_send_unary_generic(
    ClientRpcDataInternal< grpc::ByteBuffer, grpc::ByteBuffer >* data, const grpc::ByteBuffer& request,
    const std::string& method, uint32_t deadline) {
    data->set_deadline(deadline);
    if (m_token_client) { data->add_metadata(m_token_client->get_auth_header_key(), m_token_client->get_token()); }
    // Note that async unary RPCs don't post a CQ tag in call
    data->m_generic_resp_reader_ptr = m_generic_stub->PrepareUnaryCall(&data->m_context, method, request, cq());
    data->m_generic_resp_reader_ptr->StartCall();
    // CQ tag posted here
    data->m_generic_resp_reader_ptr->Finish(&data->reply(), &data->status(), (void*)data);
}

void GrpcAsyncClient::GenericAsyncStub::call_unary(const grpc::ByteBuffer& request, const std::string& method,
                                                   const generic_unary_callback_t& callback, uint32_t deadline) {
    auto data = new GenericClientRpcDataCallback(callback);
    prepare_and_send_unary_generic(data, request, method, deadline);
}

void GrpcAsyncClient::GenericAsyncStub::call_rpc(const generic_req_builder_cb_t& builder_cb, const std::string& method,
                                                 const generic_rpc_comp_cb_t& done_cb, uint32_t deadline) {
    auto cd = new GenericClientRpcData(done_cb);
    builder_cb(cd->m_req);
    prepare_and_send_unary_generic(cd, cd->m_req, method, deadline);
}

AsyncResult< grpc::ByteBuffer > GrpcAsyncClient::GenericAsyncStub::call_unary(const grpc::ByteBuffer& request,
                                                                              const std::string& method,
                                                                              uint32_t deadline) {
    auto [p, sf] = folly::makePromiseContract< Result< grpc::ByteBuffer > >();
    auto data = new GenericClientRpcDataFuture(std::move(p));
    prepare_and_send_unary_generic(data, request, method, deadline);
    return std::move(sf);
}

AsyncResult< GenericClientResponse > GrpcAsyncClient::GenericAsyncStub::call_unary(const io_blob_list_t& request,
                                                                                   const std::string& method,
                                                                                   uint32_t deadline) {
    auto [p, sf] = folly::makePromiseContract< Result< GenericClientResponse > >();
    auto data = new GenericRpcDataFutureBlob(std::move(p));
    grpc::ByteBuffer cli_byte_buf;
    serialize_to_byte_buffer(request, cli_byte_buf);
    prepare_and_send_unary_generic(data, cli_byte_buf, method, deadline);
    return std::move(sf);
}

std::unique_ptr< GrpcAsyncClient::GenericAsyncStub > GrpcAsyncClient::make_generic_stub(const std::string& worker) {
    auto w = GrpcAsyncClientWorker::get_worker(worker);
    if (w == nullptr) { throw std::runtime_error("worker thread not available"); }

    return std::make_unique< GrpcAsyncClient::GenericAsyncStub >(std::make_unique< grpc::GenericStub >(m_channel), w,
                                                                 m_token_client);
}

GenericClientResponse::GenericClientResponse(GenericClientResponse&& other) {
    m_response_buf.Swap(&(other.m_response_buf));
    m_single_slice = std::move(other.m_single_slice);
    other.m_response_buf.Release();
}

GenericClientResponse& GenericClientResponse::operator=(GenericClientResponse&& other) {
    m_response_buf.Swap(&(other.m_response_buf));
    m_single_slice = std::move(other.m_single_slice);
    other.m_response_buf.Release();
    return *this;
}

GenericClientResponse& GenericClientResponse::operator=(GenericClientResponse const& other) {
    m_response_buf = other.m_response_buf;
    m_single_slice = other.m_single_slice;
    return *this;
}

io_blob GenericClientResponse::response_blob() {
    if ((m_single_slice.size() == 0) && m_response_buf.Valid()) {
        auto status = m_response_buf.TrySingleSlice(&m_single_slice);
        if (!status.ok()) {
            status = m_response_buf.DumpToSingleSlice(&m_single_slice);
            RELEASE_ASSERT(status.ok(), "Failed to deserialize response: code: {}. msg: {}",
                           static_cast< int >(status.error_code()), status.error_message());
        }
        m_response_buf.Clear(); // Since we dumped everything to a single slice, we don't need bytebyffer anymore
    }

    auto const size = uint32_cast(m_single_slice.size());
    return size ? io_blob{m_single_slice.begin(), size, false /* is_aligned */} : io_blob{};
}

GenericRpcDataFutureBlob::GenericRpcDataFutureBlob(folly::Promise< Result< GenericClientResponse > >&& promise) :
        m_promise{std::move(promise)} {}

void GenericRpcDataFutureBlob::handle_response([[maybe_unused]] bool ok) {
    // For unary call, ok is always true, `status_` will indicate error if there are any.
    if (this->m_status.ok()) {
        auto future_resp = GenericClientResponse(this->m_reply);
        m_promise.setValue(std::move(future_resp));
    } else {
        m_promise.setValue(folly::makeUnexpected(this->m_status));
    }
}

} // namespace sisl
