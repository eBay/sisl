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
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <boost/intrusive/slist.hpp>

#include <benchmark/benchmark.h>

#include "sisl/utility/thread_buffer.hpp"
#include "sisl/wisr/wisr_ds.hpp"

THREAD_BUFFER_INIT
RCU_REGISTER_INIT

static constexpr size_t ITERATIONS{1000000};
static constexpr size_t THREADS{8};

using namespace sisl;

struct Entry : public boost::intrusive::slist_base_hook<> {
    explicit Entry(const uint64_t n) : m_n(n) {}
    uint64_t m_n;
};

std::unique_ptr< boost::intrusive::slist< Entry > > glob_lock_list;
std::mutex glob_list_mutex;

std::unique_ptr< sisl::wisr_intrusive_slist< Entry > > glob_wisr_list;
std::array< std::vector< Entry >, THREADS > glob_entries;

void setup() {
    glob_wisr_list = std::make_unique< sisl::wisr_intrusive_slist< Entry > >();
    glob_lock_list = std::make_unique< boost::intrusive::slist< Entry > >();

    for (size_t i{0}; i < THREADS; ++i) {
        auto v = &glob_entries[i];
        for (size_t j{0}; j < ITERATIONS; ++j) {
            v->emplace_back(Entry((i * ITERATIONS) + j));
        }
    }
}

void test_locked_list_insert(benchmark::State& state) {
    auto it = glob_entries[state.thread_index()].begin();
    for (auto s : state) { // Loops upto iteration count
        std::lock_guard< std::mutex > lg(glob_list_mutex);
        glob_lock_list->push_front(*it);
        ++it;
    }

    if (state.thread_index() == 0) { glob_lock_list->clear(); }
}

void test_wisr_list_insert(benchmark::State& state) {
    auto it = glob_entries[state.thread_index()].begin();
    for (auto s : state) { // Loops upto iteration count
        glob_wisr_list->push_front(*it);
        ++it;
    }

    if (state.thread_index() == 0) {
        auto l = glob_wisr_list->get_copy_and_reset();
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

int main(int argc, char** argv) {
    setup();
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}
