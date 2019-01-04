#include <benchmark/benchmark.h>
#include <mutex>
#include "wisr/wisr_ds.hpp"
#include <string>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <vector>

THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

//#define ITERATIONS 100000
#define ITERATIONS 100
#define THREADS    8

using namespace sisl;

std::unique_ptr< std::vector<uint64_t> > glob_lock_vector;
std::mutex glob_vector_mutex;

std::unique_ptr< sisl::wisr_vector< uint64_t > > glob_wisr_vector;

#define NENTRIES_PER_THREAD 200

void setup() {
    glob_lock_vector = std::make_unique< std::vector< uint64_t > >();
    glob_lock_vector->reserve(NENTRIES_PER_THREAD * THREADS * ITERATIONS);
    glob_wisr_vector = std::make_unique< sisl::wisr_vector< uint64_t > >((size_t)NENTRIES_PER_THREAD * THREADS * ITERATIONS);
}

void test_locked_vector_insert(benchmark::State& state) {
    for (auto _ : state) { // Loops upto iteration count
        //state.PauseTiming();
        //glob_lock_vector.reserve(NENTRIES_PER_THREAD * THREADS);
        //state.ResumeTiming();

        for (auto i = 0U; i < NENTRIES_PER_THREAD; i++) {
            std::lock_guard<std::mutex> lg(glob_vector_mutex);
            glob_lock_vector->emplace_back(i);
        }

        //state.PauseTiming();
        //glob_lock_vector.clear();
        //state.ResumeTiming();
    }
}

void test_wisr_vector_insert(benchmark::State &state) {
    for (auto _ : state) {
        for (auto i = 0U; i < NENTRIES_PER_THREAD; i++) {
            glob_wisr_vector->emplace_back(i);
        }
    }
}

void test_locked_vector_read(benchmark::State& state) {
    uint64_t ret;
    for (auto _ : state) { // Loops upto iteration count
        std::lock_guard<std::mutex> lg(glob_vector_mutex);
        for (auto v : *glob_lock_vector) {
            benchmark::DoNotOptimize(ret = v * 2);
        }
    }
}

void test_wisr_vector_read(benchmark::State &state) {
    uint64_t ret;
    for (auto _ : state) { // Loops upto iteration count
        auto vec = glob_wisr_vector->get_copy_and_reset();
        for (auto v : *vec) {
            benchmark::DoNotOptimize(ret = v * 2);
        }
    }
}

BENCHMARK(test_locked_vector_insert)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_wisr_vector_insert)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_locked_vector_insert)->Iterations(ITERATIONS)->Threads(1);
BENCHMARK(test_wisr_vector_insert)->Iterations(ITERATIONS)->Threads(1);
BENCHMARK(test_locked_vector_read)->Iterations(ITERATIONS)->Threads(1);
BENCHMARK(test_wisr_vector_read)->Iterations(ITERATIONS)->Threads(1);

int main(int argc, char** argv)
{
    setup();
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}