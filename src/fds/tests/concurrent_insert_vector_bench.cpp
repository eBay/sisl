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
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/fds/concurrent_insert_vector.hpp>

using namespace sisl;

static constexpr uint32_t NUM_THREADS = 1;
std::unique_ptr< std::vector< uint64_t > > glob_lock_vector;
std::mutex glob_vector_mutex;

std::unique_ptr< sisl::ConcurrentInsertVector< uint64_t > > glob_cvec;

void test_locked_vector_insert(benchmark::State& state) {
    // auto const per_thread_count = nentries / state.threads();

    LOGINFO("Running on {} iterations in {} threads", state.iterations(), state.threads());
    std::cout << "Running on iterations=" << state.iterations() << " in threads=" << state.threads() << "\n";
    glob_lock_vector = std::make_unique< std::vector< uint64_t > >();

    uint64_t i{0};
    for (auto _ : state) { // Loops upto iteration count
        std::lock_guard< std::mutex > lg(glob_vector_mutex);
        glob_lock_vector->emplace_back(++i);
    }
}

void test_concurrent_vector_insert(benchmark::State& state) {
    std::cout << "Running on iterations=" << state.iterations() << " in threads=" << state.threads() << "\n";
    glob_cvec = std::make_unique< sisl::ConcurrentInsertVector< uint64_t > >();

    uint64_t i{0};
    for (auto _ : state) { // Loops upto iteration count
        glob_cvec->emplace_back(++i);
    }
}

BENCHMARK(test_locked_vector_insert)->Threads(NUM_THREADS);
BENCHMARK(test_concurrent_vector_insert)->Threads(NUM_THREADS);

SISL_OPTIONS_ENABLE(logging)

int main(int argc, char* argv[]) {
    int parsed_argc{argc};
    ::benchmark::Initialize(&parsed_argc, argv);
    SISL_OPTIONS_LOAD(parsed_argc, argv, logging);
    sisl::logging::SetLogger("insert_vector_bench");

    // setup();
    ::benchmark::RunSpecifiedBenchmarks();
}
