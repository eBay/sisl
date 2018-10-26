/*
 * echo_async_client.cpp
 *
 *  Created on: Oct 9, 2018
 */

#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <functional>
#include <chrono>
#include <thread>
#include <atomic>
#include <cassert>


#include "sds_grpc/client.h"
#include "sds_grpc_test.grpc.pb.h"


using namespace ::grpc;
using namespace ::sds::grpc;
using namespace ::sds_grpc_test;
using namespace std::placeholders;


class EchoAsyncClient : public GrpcConnection<::sds_grpc_test::EchoService>
{
public:
    EchoAsyncClient(const std::string& server_addr, uint32_t dead_line,
            ::grpc::CompletionQueue* cq,
            const std::string& target_domain,
            const std::string& ssl_cert)
        : GrpcConnection<::sds_grpc_test::EchoService>(
                server_addr, dead_line, cq, target_domain, ssl_cert)
    {

    }


    void Echo(const EchoRequest& request,
            std::function<void(EchoReply&, ::grpc::Status& status)> callback)
    {
        auto call = new ClientCallData<EchoRequest, EchoReply>(callback);
        call->set_deadline(dead_line_);
        call->responder_reader() = stub()->AsyncEcho(
                &call->context(), request, completion_queue());
        call->responder_reader()->Finish(&call->reply(), &call->status(), (void*)call);
    }


};


std::atomic_int g_counter;

class Ping
{
public:

    Ping(int seqno)
    {
        request_.set_message(std::to_string(seqno));
    }

    void handle_echo_reply(EchoReply& reply, ::grpc::Status& status)
    {
        if (!status.ok())
        {
            std::cout << "echo request " << request_.message() <<
                    " failed, status " << status.error_code() <<
                    ": " << status.error_message() << std::endl;
            return;
        }

        std::cout << "echo request " << request_.message() <<
                " reply " << reply.message() << std::endl;


        assert(request_.message() == reply.message());
        g_counter.fetch_add(1, std::memory_order_relaxed);
    }

    EchoRequest request_;
};


int RunClient(const std::string& server_address)
{
    GrpcClient* fix_this_name = new GrpcClient();
    auto client = GrpcConnectionFactory::Make<EchoAsyncClient>(
            server_address, 5, &(fix_this_name->cq()), "", "");

    if (!client)
    {
        std::cout << "Create echo async client failed." << std::endl;
        return -1;
    }

    fix_this_name->run(3);

    for (int i = 0; i < 10; i++)
    {
        Ping * ping = new Ping(i);
        client->Echo(ping->request_, std::bind(&Ping::handle_echo_reply, ping, _1, _2));
    }

    delete fix_this_name; // wait client worker threads terminate

    return g_counter.load();

}


int main(int argc, char** argv) {

    std::string server_address("0.0.0.0:50051");


    return RunClient(server_address);
}


