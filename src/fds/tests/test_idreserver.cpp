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
#include <thread>
#include <vector>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include "id_reserver.hpp"

#include <gtest/gtest.h>

using namespace sisl;

SISL_LOGGING_INIT(test_bitset);

SISL_OPTIONS_ENABLE(logging, test_id_reserver)

SISL_OPTION_GROUP(test_id_reserver,
                 (num_threads, "", "num_threads", "number of threads",
                  ::cxxopts::value< uint32_t >()->default_value("8"), "number"),
                 (max_ids, "", "max_ids", "maximum number of ids",
                  ::cxxopts::value< uint32_t >()->default_value("1000"), "number"),

namespace {
    uint32_t g_max_ids;
    uint32_t g_num_threads;

    void run_parallel(uint32_t nthreads, const std::function< void(uint32_t) >& thr_fn) {
        std::vector< std::thread > threads;
        auto n_per_thread = std::ceil((double)g_max_ids / nthreads);
        int32_t remain_ids = (int32_t)g_max_ids;

        while (remain_ids > 0) {
            threads.emplace_back(thr_fn, std::min(remain_ids, (int32_t)n_per_thread));
            remain_ids -= n_per_thread;
        }
        for (auto t : threads) {
            if (t.joinable()) t.join();
        }
    }

    struct IDReserverTest : public testing::Test {
    public:
        IDReserverTest(const IDReserverTest&) = delete;
        IDReserverTest(IDReserverTest&&) noexcept = delete;
        IDReserverTest& operator=(const IDReserverTest&) = delete;
        IDReserverTest& operator=(IDReserverTest&&) noexcept = delete;
        virtual ~IDReserverTest() override = default;

    protected:
        IDReserver m_reserver;

        void SetUp() override {}
        void TearDown() override {}
    };
}

TEST_F(IDReserverTest, RandomIDSet) {
    run_parallel(g_num_threads, [&](int32_t n_ids_this_thread) {
        LOGINFO("INFO: Setting alternate bits (set even and reset odd) in range[{} - {}]", start, start + count - 1);
        for (auto i = 0; i < n_ids_this_thread; ++i) {
            if (i % 2 == 0) { reserve(); }
        }
    });
}

int main(int argc, char* argv[]) {
    SISL_OPTIONS_LOAD(argc, argv, logging, test_id_reserver);
    ::testing::InitGoogleTest(&argc, argv);
    sisl::logging::SetLogger("test_id_reserver");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    g_max_ids = SISL_OPTIONS["max_ids"].as< uint32_t >();
    g_num_threads = SISL_OPTIONS["num_threads"].as< uint32_t >();

    auto ret = RUN_ALL_TESTS();
    return ret;
}
