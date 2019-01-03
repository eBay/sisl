#include <benchmark/benchmark.h>
#include <mutex>
//#include "fds/wisr_list.hpp"
#include "fds/wisr_ds.hpp"
#include <string>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <list>

THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

//#define ITERATIONS 100000
#define ITERATIONS 100
#define THREADS    8

using namespace sisl;
using namespace sisl::fds;

std::unique_ptr< std::list<uint64_t> > glob_lock_list;
std::mutex glob_list_mutex;

std::unique_ptr< sisl::fds::wisr_list< uint64_t > > glob_wisr_list;

using namespace sisl;
using namespace sisl::fds;

#define NENTRIES_PER_THREAD 20000

void setup() {
    glob_lock_list = std::make_unique< std::list< uint64_t > >();
    glob_wisr_list = std::make_unique< sisl::fds::wisr_list< uint64_t > >();
}

void test_locked_list_insert(benchmark::State& state) {
    for (auto _ : state) { // Loops upto iteration count
        //state.PauseTiming();
        //glob_lock_list.reserve(NENTRIES_PER_THREAD * THREADS);
        //state.ResumeTiming();

        for (auto i = 0U; i < NENTRIES_PER_THREAD; i++) {
            std::lock_guard<std::mutex> lg(glob_list_mutex);
            glob_lock_list->emplace_back(i);
        }

        //state.PauseTiming();
        //glob_lock_list.clear();
        //state.ResumeTiming();
    }
}

void test_wisr_list_insert(benchmark::State &state) {
    for (auto _ : state) {
        for (auto i = 0U; i < NENTRIES_PER_THREAD; i++) {
            glob_wisr_list->emplace_back(i);
        }
    }
}

void test_locked_list_read(benchmark::State& state) {
    uint64_t ret;
    for (auto _ : state) { // Loops upto iteration count
        std::lock_guard<std::mutex> lg(glob_list_mutex);
        for (auto v : *glob_lock_list) {
            benchmark::DoNotOptimize(ret = v * 2);
        }
    }
}

void test_wisr_list_read(benchmark::State &state) {
    uint64_t ret;
    for (auto _ : state) { // Loops upto iteration count
        auto vec = glob_wisr_list->get_copy();
        for (auto v : *vec) {
            benchmark::DoNotOptimize(ret = v * 2);
        }
    }
}

BENCHMARK(test_locked_list_insert)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_wisr_list_insert)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_locked_list_insert)->Iterations(ITERATIONS)->Threads(1);
BENCHMARK(test_wisr_list_insert)->Iterations(ITERATIONS)->Threads(1);
BENCHMARK(test_locked_list_read)->Iterations(ITERATIONS)->Threads(1);
BENCHMARK(test_wisr_list_read)->Iterations(ITERATIONS)->Threads(1);

int main(int argc, char** argv)
{
    setup();
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}