#include <benchmark/benchmark.h>
#include <mutex>
#include "fds/wisr_ds.hpp"
#include <string>
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/intrusive/slist.hpp>
#include <cstdio>

THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

#define ITERATIONS 1000000
//#define ITERATIONS 1
#define THREADS    8

using namespace sisl;
using namespace sisl::fds;

struct Entry : public boost::intrusive::slist_base_hook<> {
    explicit Entry(uint64_t n) : m_n(n) {}
    uint64_t m_n;
};

std::unique_ptr< boost::intrusive::slist< Entry > > glob_lock_list;
std::mutex glob_list_mutex;

std::unique_ptr< sisl::fds::wisr_intrusive_slist< Entry > > glob_wisr_list;
std::array< std::vector< Entry >, THREADS > glob_entries;

using namespace sisl;
using namespace sisl::fds;

void setup() {
    glob_wisr_list = std::make_unique< sisl::fds::wisr_intrusive_slist< Entry > >();
    glob_lock_list = std::make_unique< boost::intrusive::slist< Entry > >();

    for (auto i = 0U; i < THREADS; i++) {
        auto v = &glob_entries[i];
        for (auto j = 0UL; j < ITERATIONS; j++) {
            v->emplace_back(Entry((i * ITERATIONS) + j));
        }
    }
}

void test_locked_list_insert(benchmark::State& state) {
    auto it = glob_entries[state.thread_index].begin();
    for (auto _ : state) { // Loops upto iteration count
        std::lock_guard<std::mutex> lg(glob_list_mutex);
        glob_lock_list->push_front(*it);
        ++it;
    }

    if (state.thread_index == 0) {
        glob_lock_list->clear();
    }
}

void test_wisr_list_insert(benchmark::State &state) {
    auto it = glob_entries[state.thread_index].begin();
    for (auto _ : state) { // Loops upto iteration count
        glob_wisr_list->push_front(*it);
        ++it;
    }

    if (state.thread_index == 0) {
        auto l = glob_wisr_list->get_copy();
        l->clear();
    }
}

#if 0
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
#endif

BENCHMARK(test_wisr_list_insert)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_locked_list_insert)->Iterations(ITERATIONS)->Threads(THREADS);
#if 0
BENCHMARK(test_locked_list_insert)->Iterations(ITERATIONS)->Threads(1);
BENCHMARK(test_wisr_list_insert)->Iterations(ITERATIONS)->Threads(1);
BENCHMARK(test_locked_list_read)->Iterations(ITERATIONS)->Threads(1);
BENCHMARK(test_wisr_list_read)->Iterations(ITERATIONS)->Threads(1);
#endif

int main(int argc, char** argv)
{
    setup();
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}