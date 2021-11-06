//
// Created by Kadayam, Hari on 2/1/19.
//

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <random>
#include <string>

#include <benchmark/benchmark.h>
#include <sds_logging/logging.h>
#include <sds_options/options.h>

#include "metrics/metrics.hpp"
#include "obj_allocator.hpp"

SDS_LOGGING_INIT(HOMESTORE_LOG_MODS)
RCU_REGISTER_INIT

namespace {
std::mutex s_print_mutex;
constexpr size_t ITERATIONS{1000000};
constexpr size_t THREADS{8};

struct my_request {
    int m_a;
    int m_b[10];
    std::string m_c;
    uint64_t m_d;
};

void setup() {}
void test_malloc(benchmark::State& state) {
    uint64_t counter{0};
    static thread_local std::random_device rd{};
    static thread_local std::default_random_engine engine{rd()};

    for (auto [[maybe_unused]] si : state) { // Loops up to iteration count
        my_request* req;
        benchmark::DoNotOptimize(req = new my_request());
        req->m_a = 10;
        req->m_b[0] = 100;
        std::uniform_int_distribution< uint64_t > dist{0, RAND_MAX};
        req->m_d = req->m_a * dist(engine);
        counter += req->m_d;
        delete (req);
    }
    {
        std::scoped_lock< std::mutex > lock{s_print_mutex};
        std::cout << "Counter = " << counter << std::endl;
    }
}

void test_obj_alloc(benchmark::State& state) {
    uint64_t counter{0};
    static thread_local std::random_device rd{};
    static thread_local std::default_random_engine engine{rd()};
    for (auto [[maybe_unused]] si : state) { // Loops up to iteration count
        my_request* req;
        benchmark::DoNotOptimize(req = sisl::ObjectAllocator< my_request >::make_object());
        req->m_a = 10;
        req->m_b[0] = 100;
        std::uniform_int_distribution< uint64_t > dist{0, RAND_MAX};
        req->m_d = req->m_a * dist(engine);
        counter += req->m_d;
        sisl::ObjectAllocator< my_request >::deallocate(req);
    }
    {
        std::scoped_lock< std::mutex > lock{s_print_mutex};
        std::cout << "Counter = " << counter << std::endl;
    }
}
} // namespace

BENCHMARK(test_malloc)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_obj_alloc)->Iterations(ITERATIONS)->Threads(THREADS);

SDS_OPTIONS_ENABLE(logging)
int main(int argc, char** argv) {
    SDS_OPTIONS_LOAD(argc, argv, logging)
    setup();
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    // std::cout << "Metrics: " << sisl::MetricsFarm::getInstance().get_result_in_json().dump(4) << "\n";
}
