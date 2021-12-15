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
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <benchmark/benchmark.h>

#include "utility/thread_buffer.hpp"
#include "wisr/wisr_ds.hpp"

THREAD_BUFFER_INIT
RCU_REGISTER_INIT

using namespace sisl;

std::unique_ptr< std::vector< uint64_t > > glob_lock_vector;
std::mutex glob_vector_mutex;

std::unique_ptr< sisl::wisr_vector< uint64_t > > glob_wisr_vector;

//#define ITERATIONS 100000
static constexpr size_t ITERATIONS{100};
static constexpr size_t THREADS{8};
static constexpr size_t NENTRIES_PER_THREAD{200};

void setup() {
    glob_lock_vector = std::make_unique< std::vector< uint64_t > >();
    glob_lock_vector->reserve(NENTRIES_PER_THREAD * THREADS * ITERATIONS);
    glob_wisr_vector =
        std::make_unique< sisl::wisr_vector< uint64_t > >((size_t)NENTRIES_PER_THREAD * THREADS * ITERATIONS);
}

void test_locked_vector_insert(benchmark::State& state) {
    for (auto s : state) { // Loops upto iteration count
        // state.PauseTiming();
        // glob_lock_vector.reserve(NENTRIES_PER_THREAD * THREADS);
        // state.ResumeTiming();

        for (size_t i{0}; i < NENTRIES_PER_THREAD; ++i) {
            std::lock_guard< std::mutex > lg(glob_vector_mutex);
            glob_lock_vector->emplace_back(i);
        }

        // state.PauseTiming();
        // glob_lock_vector.clear();
        // state.ResumeTiming();
    }
}

void test_wisr_vector_insert(benchmark::State& state) {
    for (auto s : state) {
        for (size_t i{0}; i < NENTRIES_PER_THREAD; ++i) {
            glob_wisr_vector->emplace_back(i);
        }
    }
}

void test_locked_vector_read(benchmark::State& state) {
    uint64_t ret;
    for (auto s : state) { // Loops upto iteration count
        std::lock_guard< std::mutex > lg(glob_vector_mutex);
        for (auto& v : *glob_lock_vector) {
            benchmark::DoNotOptimize(ret = v * 2);
        }
    }
}

void test_wisr_vector_read(benchmark::State& state) {
    uint64_t ret;
    for (auto s : state) { // Loops upto iteration count
        auto vec = glob_wisr_vector->get_copy_and_reset();
        for (auto& v : *vec) {
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

int main(int argc, char** argv) {
    setup();
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}
