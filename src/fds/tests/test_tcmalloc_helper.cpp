/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Bryan Zimmerman
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
#ifdef USING_TCMALLOC
#include <cstdint>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>

#include "sisl/logging/logging.h"
#include "sisl/options/options.h"
#include "sisl/utility/thread_buffer.hpp"

#include <gtest/gtest.h>

#include "sisl/fds/malloc_helper.hpp"

using namespace sisl;

namespace {
uint32_t g_num_threads;

struct TcmallocTest : public testing::Test {
public:
    TcmallocTest() : testing::Test{} { LOGINFO("Initializing new TcmallocTest class"); }
    TcmallocTest(const TcmallocTest&) = delete;
    TcmallocTest(TcmallocTest&&) noexcept = delete;
    TcmallocTest& operator=(const TcmallocTest&) = delete;
    TcmallocTest& operator=(TcmallocTest&&) noexcept = delete;
    virtual ~TcmallocTest() override = default;

protected:
    void SetUp() override {}
    void TearDown() override {}

    void MultiThreadedAllocDealloc(const size_t iterations, const size_t mem_count = 1000000) const {
        const auto thread_lambda{[&iterations, &mem_count]() {
            // allocated/deallocate memory
            for (size_t iteration{0}; iteration < iterations; ++iteration) {
                std::unique_ptr< uint64_t[] > mem{new uint64_t[mem_count]};
            }
        }};

        std::vector< std::thread > threads;
        for (uint32_t thread_num{0}; thread_num < g_num_threads; ++thread_num) {
            threads.emplace_back(thread_lambda);
        }

        for (auto& alloc_dealloc_thread : threads) {
            if (alloc_dealloc_thread.joinable()) alloc_dealloc_thread.join();
        };
    }
};
} // namespace

TEST_F(TcmallocTest, GetDirtyPageCount) { MultiThreadedAllocDealloc(100); }

SISL_OPTIONS_ENABLE(logging, test_tcmalloc)

SISL_OPTION_GROUP(test_tcmalloc,
                  (num_threads, "", "num_threads", "number of threads",
                   ::cxxopts::value< uint32_t >()->default_value("8"), "number"))

int main(int argc, char* argv[]) {
    SISL_OPTIONS_LOAD(argc, argv, logging, test_tcmalloc);
    ::testing::InitGoogleTest(&argc, argv);
    sisl::logging::SetLogger("test_bitset");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    g_num_threads = SISL_OPTIONS["num_threads"].as< uint32_t >();

    const auto ret{RUN_ALL_TESTS()};
    return ret;
}

#endif
