/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam
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
#include <cstdlib>
#include <memory>
#include <mutex>

#include <benchmark/benchmark.h>

#include "utility/thread_buffer.hpp"
#include "wisr/wisr_ds.hpp"

THREAD_BUFFER_INIT
RCU_REGISTER_INIT

//#define ITERATIONS 100000
static constexpr size_t ITERATIONS{100};
static constexpr size_t THREADS{8};

using namespace sisl;

std::unique_ptr< std::list< uint64_t > > glob_lock_list;
std::mutex glob_list_mutex;

std::unique_ptr< sisl::wisr_list< uint64_t > > glob_wisr_list;

#define NENTRIES_PER_THREAD 20000

void setup() {
    glob_lock_list = std::make_unique< std::list< uint64_t > >();
    glob_wisr_list = std::make_unique< sisl::wisr_list< uint64_t > >();
}

void test_locked_list_insert(benchmark::State& state) {
    for (auto s : state) { // Loops upto iteration count
        // state.PauseTiming();
        // glob_lock_list.reserve(NENTRIES_PER_THREAD * THREADS);
        // state.ResumeTiming();

        for (size_t i{0}; i < NENTRIES_PER_THREAD; ++i) {
            std::lock_guard< std::mutex > lg(glob_list_mutex);
            glob_lock_list->emplace_back(i);
        }

        // state.PauseTiming();
        // glob_lock_list.clear();
        // state.ResumeTiming();
    }
}

void test_wisr_list_insert(benchmark::State& state) {
    for (auto s : state) {
        for (size_t i{0}; i < NENTRIES_PER_THREAD; ++i) {
            glob_wisr_list->emplace_back(i);
        }
    }
}

void test_locked_list_read(benchmark::State& state) {
    uint64_t ret;
    for (auto s : state) { // Loops upto iteration count
        std::lock_guard< std::mutex > lg(glob_list_mutex);
        for (auto& v : *glob_lock_list) {
            benchmark::DoNotOptimize(ret = v * 2);
        }
    }
}

void test_wisr_list_read(benchmark::State& state) {
    uint64_t ret;
    for (auto s : state) { // Loops upto iteration count
        auto vec = glob_wisr_list->get_copy_and_reset();
        for (auto& v : *vec) {
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

int main(int argc, char** argv) {
    setup();
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}
