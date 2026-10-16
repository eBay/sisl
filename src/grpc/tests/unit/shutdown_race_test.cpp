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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include "sisl/grpc/rpc_client.hpp"
#include "sisl/grpc/rpc_server.hpp"
#include "grpc_helper_test.grpc.pb.h"

SISL_LOGGING_INIT(logging, grpc_server)
SISL_OPTIONS_ENABLE(logging)

namespace sisl::grpc::testing {
using namespace sisl;
using namespace ::grpc_helper_test;

namespace {
constexpr uint32_t num_iterations = 50;
constexpr uint32_t num_client_threads = 4;
constexpr uint16_t base_port = 19600;
constexpr char worker_name[] = "shutdown_race_worker";

class EchoServiceImpl final {
public:
    bool echo_request(const AsyncRpcDataPtr< EchoService, EchoRequest, EchoReply >& rpc_data) {
        rpc_data->response().set_message(rpc_data->request().message());
        return true;
    }

    bool register_service(GrpcServer* server) { return server->register_async_service< EchoService >(); }

    bool register_rpcs(GrpcServer* server) {
        return server->register_rpc< EchoService, EchoRequest, EchoReply, false >(
            "Echo", &EchoService::AsyncService::RequestEcho,
            std::bind(&EchoServiceImpl::echo_request, this, std::placeholders::_1));
    }
};
} // namespace

// Regression test for the TOCTOU race between GrpcServer::handle_rpcs()'s
// "check shutdown state, then either delete or re-register the next listener" sequence
// and GrpcServer::shutdown()'s state flip. Before the fix, a shutdown() landing in that
// window let enqueue_call_request() call grpc_server_request_call() after Server::Shutdown()
// had already been issued; grpc silently drops such calls instead of ever firing their
// completion tag, which leaves the RpcData object (and everything it owns: protobuf
// Arenas, the queued response container, gRPC's own call metadata) permanently stuck at
// ref=1 and never freed. This is exactly what LeakSanitizer caught in HomeObject's
// homestore_test_gc.
//
// There is no seam to deterministically force the race from outside GrpcServer, so
// instead this test maximizes the odds of hitting it: many iterations, each keeping a
// bounded burst of concurrent RPCs in flight (see max_requests_per_thread /
// fire_deadline below) and calling shutdown() with (deliberately) no synchronization
// against them. Under a sanitized (ASAN/LSan) build, a regression here is expected to
// surface as a leak in the process-wide check done at exit — the same way the original
// bug was found. This also guards against the deadlock introduced by an earlier version
// of the fix, which held the same mutex across the blocking Server::Shutdown() call:
// that version would hang this test instead of letting it complete, since Shutdown()
// needs the very thread blocked on the mutex to keep draining its completion queue.
TEST(ShutdownRaceTest, repeated_traffic_during_shutdown) {
    GrpcAsyncClientWorker::create_worker(worker_name, num_client_threads);

    for (uint32_t iter = 0; iter < num_iterations; ++iter) {
        const std::string server_address = fmt::format("127.0.0.1:{}", base_port + (iter % 100));

        auto* server = GrpcServer::make(server_address, /* threads = */ 2, "", "");
        EchoServiceImpl echo_impl;
        ASSERT_TRUE(echo_impl.register_service(server));
        server->run();
        ASSERT_TRUE(echo_impl.register_rpcs(server));

        auto client = std::make_unique< GrpcAsyncClient >(server_address, "", "");
        client->init();
        auto stub = client->make_stub< EchoService >(worker_name);

        std::atomic< int64_t > inflight{0};
        std::mutex done_mtx;
        std::condition_variable done_cv;

        // Each traffic thread stops on its own, bounded by count and wall-clock time —
        // deliberately NOT by a flag flipped after shutdown() returns. shutdown() (no
        // deadline) blocks until all accepted calls finish, so if the traffic threads
        // only stopped once shutdown() returned, and a client-visible new call could
        // still land on the server during the shutdown race window, the two would wait
        // on each other forever: a self-inflicted livelock in the test, not in
        // GrpcServer. Bounding traffic independently of shutdown()'s completion avoids
        // that regardless of how grpc happens to behave in that window.
        constexpr uint32_t max_requests_per_thread = 300;
        const auto fire_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);

        std::vector< std::thread > traffic_threads;
        for (uint32_t t = 0; t < num_client_threads; ++t) {
            traffic_threads.emplace_back([&, t]() {
                for (uint32_t seq = 0;
                     seq < max_requests_per_thread && std::chrono::steady_clock::now() < fire_deadline; ++seq) {
                    EchoRequest req;
                    req.set_message(fmt::format("msg-{}-{}", t, seq));
                    inflight.fetch_add(1, std::memory_order_relaxed);
                    stub->call_unary< EchoRequest, EchoReply >(
                        req, &EchoService::StubInterface::AsyncEcho,
                        [&inflight, &done_cv](EchoReply&, ::grpc::Status&) {
                            if (inflight.fetch_sub(1, std::memory_order_relaxed) == 1) { done_cv.notify_all(); }
                        },
                        /* deadline secs = */ 2);
                }
            });
        }

        // Give the traffic threads a moment to get requests flowing, then shut the
        // server down right underneath them without any synchronization — that overlap
        // is the whole point of this test.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        server->shutdown();

        for (auto& t : traffic_threads) {
            t.join();
        }

        // Drain whatever calls were still in flight when shutdown() returned. grpc
        // guarantees exactly one terminal callback per call (success, error, or
        // cancellation), bounded by the per-call deadline above, so this should never
        // block indefinitely; the timeout here is just a safety net against a deadlock
        // regression rather than something expected to fire.
        {
            std::unique_lock lk(done_mtx);
            const bool drained = done_cv.wait_for(lk, std::chrono::seconds(10),
                                                  [&]() { return inflight.load(std::memory_order_relaxed) == 0; });
            EXPECT_TRUE(drained) << "iteration " << iter << ": in-flight calls never drained after shutdown "
                                 << "(possible deadlock in GrpcServer::shutdown())";
        }

        client.reset();
        delete server;
    }

    GrpcAsyncClientWorker::shutdown_all();
}

} // namespace sisl::grpc::testing

int main(int argc, char* argv[]) {
    ::testing::InitGoogleMock(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger("shutdown_race_test");
    return RUN_ALL_TESTS();
}
