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
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <thread>
#include <vector>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/fds/id_reserver.hpp>

#include <gtest/gtest.h>

using namespace sisl;

SISL_LOGGING_INIT(test_id_reserver)

SISL_OPTIONS_ENABLE(logging, test_id_reserver)

SISL_OPTION_GROUP(test_id_reserver,
                  (num_threads, "", "num_threads", "number of threads",
                   ::cxxopts::value< uint32_t >()->default_value("8"), "number"),
                  (max_ids, "", "max_ids", "maximum number of ids",
                   ::cxxopts::value< uint32_t >()->default_value("1000"), "number"))

namespace {
uint32_t g_max_ids;
uint32_t g_num_threads;

void run_parallel(uint32_t nthreads, const std::function< void(uint32_t) >& thr_fn) {
    std::vector< std::thread > threads;
    auto const n_per_thread = (uint32_t)std::ceil((double)g_max_ids / nthreads);
    uint32_t remain_ids = g_max_ids;

    while (remain_ids > 0) {
        auto const n = std::min(remain_ids, n_per_thread);
        threads.emplace_back(thr_fn, n);
        remain_ids -= n;
    }
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
}

struct IDReserverTest : public testing::Test {
protected:
    IDReserver m_reserver;
};
} // namespace

TEST_F(IDReserverTest, ConcurrentReserve) {
    std::vector< uint32_t > reserved(g_max_ids);
    std::atomic< uint32_t > slot{0};

    run_parallel(g_num_threads, [&](uint32_t n) {
        for (uint32_t i{0}; i < n; ++i) {
            reserved[slot.fetch_add(1, std::memory_order_relaxed)] = m_reserver.reserve();
        }
    });

    std::sort(reserved.begin(), reserved.end());
    for (size_t i{0}; i < reserved.size(); ++i) {
        EXPECT_TRUE(m_reserver.is_reserved(reserved[i]));
        if (i > 0) { EXPECT_NE(reserved[i], reserved[i - 1]); }
    }
}

TEST_F(IDReserverTest, ReserveUnreserveReuse) {
    auto const id1 = m_reserver.reserve();
    auto const id2 = m_reserver.reserve();
    EXPECT_NE(id1, id2);

    m_reserver.unreserve(id1);
    EXPECT_FALSE(m_reserver.is_reserved(id1));

    // Freed slot should be reused
    auto const id3 = m_reserver.reserve();
    EXPECT_EQ(id3, id1);
}

TEST_F(IDReserverTest, SpecificReserve) {
    m_reserver.reserve(42u);
    EXPECT_TRUE(m_reserver.is_reserved(42u));
    m_reserver.unreserve(42u);
    EXPECT_FALSE(m_reserver.is_reserved(42u));
}

TEST_F(IDReserverTest, SerializeDeserialize) {
    for (uint32_t i{0}; i < 16; ++i) {
        m_reserver.reserve();
    }
    m_reserver.unreserve(3);
    m_reserver.unreserve(7);

    auto const buf = m_reserver.serialize();
    IDReserver restored{buf};

    for (uint32_t i{0}; i < 16; ++i) {
        EXPECT_EQ(restored.is_reserved(i), m_reserver.is_reserved(i));
    }
}

int main(int argc, char* argv[]) {
    SISL_OPTIONS_LOAD(argc, argv, logging, test_id_reserver);
    ::testing::InitGoogleTest(&argc, argv);
    sisl::logging::SetLogger("test_id_reserver");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    g_max_ids = SISL_OPTIONS["max_ids"].as< uint32_t >();
    g_num_threads = SISL_OPTIONS["num_threads"].as< uint32_t >();

    return RUN_ALL_TESTS();
}
