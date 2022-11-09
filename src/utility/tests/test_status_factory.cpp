#include <iostream>
#include <benchmark/benchmark.h>
#include <shared_mutex>
#include <atomic>
#include <thread>

#include "sisl/utility/urcu_helper.hpp"

RCU_REGISTER_INIT

#define ITERATIONS 10000
#define THREADS 8

using namespace sisl;

struct MyStatus {
    bool is_shutdown = false;
    bool is_paused = false;

    MyStatus() = default;
    MyStatus(const MyStatus& other) = default;
};

struct AtomicStatus {
    std::atomic< bool > is_shutdown = false;
    std::atomic< bool > is_paused = false;
};

#if 0
int main(int argc, char* argv[]) {
    sisl::urcu_scoped_ptr< MyStatus > status;
    std::cout << "before is_shutdown = " << status.access()->is_shutdown << "\n";
    status.update([](MyStatus* s) { s->is_shutdown = true; });
    std::cout << "after is_shutdown = " << status.access()->is_shutdown << "\n";
}
#endif

#define NENTRIES_PER_THREAD 50

std::unique_ptr< AtomicStatus > glob_atomic_status;
sisl::urcu_scoped_ptr< MyStatus > glob_urcu_status;
std::unique_ptr< MyStatus > glob_raw_status;
std::shared_mutex glob_mutex;

void setup() {
    glob_atomic_status = std::make_unique< AtomicStatus >();
    glob_raw_status = std::make_unique< MyStatus >();
}

void parallel_update() {
    auto t = std::thread([]() {
        for (auto i = 0U; i < NENTRIES_PER_THREAD; i++) {
            glob_atomic_status->is_shutdown.store(true);
            glob_atomic_status->is_paused.store(true);

            glob_raw_status->is_shutdown = true;
            glob_raw_status->is_paused = true;

            glob_urcu_status.update([](MyStatus* s) {
                s->is_shutdown = true;
                s->is_paused = true;
            });

            {
                std::unique_lock l(glob_mutex);
                glob_raw_status->is_shutdown = true;
                glob_raw_status->is_paused = true;
            }
        }
        std::cout << "Updated all status\n";
    });
    t.detach();
}

void test_atomic_gets(benchmark::State& state) {
    auto t = std::thread([]() {
        for (auto i = 0U; i < NENTRIES_PER_THREAD; i++) {
            glob_atomic_status->is_shutdown.store(true);
            glob_atomic_status->is_paused.store(true);
        }
        std::cout << "Updated all status\n";
    });

    for (auto _ : state) { // Loops upto iteration count
        // state.PauseTiming();
        // glob_lock_deque.reserve(NENTRIES_PER_THREAD * THREADS);
        // state.ResumeTiming();

        bool ret;
        for (auto i = 0U; i < NENTRIES_PER_THREAD; i++) {
            benchmark::DoNotOptimize(ret = glob_atomic_status->is_shutdown.load());
            benchmark::DoNotOptimize(ret = glob_atomic_status->is_paused.load());
        }

        // state.PauseTiming();
        // glob_lock_deque.clear();
        // state.ResumeTiming();
    }
    t.join();
}

void test_urcu_gets(benchmark::State& state) {
    rcu_register_thread();
    auto t = std::thread([]() {
        rcu_register_thread();
        for (auto i = 0U; i < NENTRIES_PER_THREAD; i++) {
            glob_urcu_status.update([](MyStatus* s) {
                s->is_shutdown = true;
                s->is_paused = true;
            });
        }
        std::cout << "Updated all status\n";
        rcu_unregister_thread();
    });

    for (auto _ : state) {
        bool ret;
        for (auto i = 0U; i < NENTRIES_PER_THREAD; i++) {
            /*rcu_read_lock();
            benchmark::DoNotOptimize(ret = glob_raw_status->is_shutdown);
            benchmark::DoNotOptimize(ret = glob_raw_status->is_paused);
            rcu_read_unlock(); */
            benchmark::DoNotOptimize(ret = glob_urcu_status.access()->is_shutdown);
            benchmark::DoNotOptimize(ret = glob_urcu_status.access()->is_paused);
        }
    }
    t.join();
    rcu_unregister_thread();
}

void test_raw_gets(benchmark::State& state) {
    parallel_update();
    for (auto _ : state) {
        bool ret;
        for (auto i = 0U; i < NENTRIES_PER_THREAD; i++) {
            benchmark::DoNotOptimize(ret = glob_raw_status->is_shutdown);
            benchmark::DoNotOptimize(ret = glob_raw_status->is_paused);
        }
    }
}

void test_mutex_gets(benchmark::State& state) {
    parallel_update();
    for (auto _ : state) {
        bool ret;
        for (auto i = 0U; i < NENTRIES_PER_THREAD; i++) {
            std::shared_lock< std::shared_mutex > l(glob_mutex);
            benchmark::DoNotOptimize(ret = glob_raw_status->is_shutdown);
            benchmark::DoNotOptimize(ret = glob_raw_status->is_paused);
        }
    }
}

#if 0
void test_locked_deque_read(benchmark::State& state) {
    uint64_t ret;
    for (auto _ : state) { // Loops upto iteration count
        std::lock_guard< std::mutex > lg(glob_deque_mutex);
        for (auto v : *glob_lock_deque) {
            benchmark::DoNotOptimize(ret = v * 2);
        }
    }
}

void test_wisr_deque_read(benchmark::State& state) {
    uint64_t ret;
    for (auto _ : state) { // Loops upto iteration count
        auto vec = glob_wisr_deque->get_copy_and_reset();
        for (auto v : *vec) {
            benchmark::DoNotOptimize(ret = v * 2);
        }
    }
}
#endif

BENCHMARK(test_atomic_gets)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_urcu_gets)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_raw_gets)->Iterations(ITERATIONS)->Threads(THREADS);
BENCHMARK(test_mutex_gets)->Iterations(ITERATIONS)->Threads(THREADS);
// BENCHMARK(test_atomic_updates)->Iterations(ITERATIONS)->Threads(1);
// BENCHMARK(test_urcu_updates)->Iterations(ITERATIONS)->Threads(1);

int main(int argc, char** argv) {
    setup();
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}
