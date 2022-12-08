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
#include <random>
#include <thread>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "sisl/fds/thread_vector.hpp"
#include "sisl/fds/stream_tracker.hpp"

using namespace sisl;

SISL_LOGGING_INIT(test_stream_tracker)

namespace {
struct TestData {
    TestData(int val) : m_value(val) {}
    int m_value = 0;
};

struct StreamTrackerTest : public testing::Test {
public:
    StreamTrackerTest(const StreamTrackerTest&) = delete;
    StreamTrackerTest(StreamTrackerTest&&) noexcept = delete;
    StreamTrackerTest& operator=(const StreamTrackerTest&) = delete;
    StreamTrackerTest& operator=(StreamTrackerTest&&) noexcept = delete;
    virtual ~StreamTrackerTest() override = default;

protected:
    StreamTracker< TestData > m_tracker;

    void SetUp() override {}
    void TearDown() override {}

public:
    StreamTrackerTest() {}
    size_t get_mem_size() {
        auto json = MetricsFarm::getInstance().get_result_in_json();
        return (size_t)json["StreamTracker"]["StreamTracker_2"]["Gauges"]["Total Memsize for stream tracker"];
    }
};
} // namespace

TEST_F(StreamTrackerTest, SimpleCompletions) {
    static std::random_device s_rd{};
    static std::default_random_engine s_engine{s_rd()};
    std::uniform_int_distribution< int64_t > gen{0, 999};

    for (auto i = 0; i < 100; ++i) {
        m_tracker.create_and_complete(i, gen(s_engine));
    }
    EXPECT_EQ(m_tracker.completed_upto(), 99);
    m_tracker.truncate();
    EXPECT_EQ(m_tracker.completed_upto(), 99);

    // Do it in reverse
    for (auto i = 150; i >= 100; --i) {
        EXPECT_EQ(m_tracker.completed_upto(), 99);
        m_tracker.create_and_complete(i, gen(s_engine));
    }
    EXPECT_EQ(m_tracker.completed_upto(), 150);

    // Do it in alternate fashion
    auto start_idx = 151;
    auto end_idx = 200;
    bool front = true;
    while (start_idx < end_idx) {
        if (front) {
            m_tracker.create_and_complete(start_idx++, gen(s_engine));
        } else {
            m_tracker.create_and_complete(end_idx--, gen(s_engine));
        }
        EXPECT_EQ(m_tracker.completed_upto(), start_idx - 1);
        front = !front;
    }
    m_tracker.create_and_complete(start_idx, gen(s_engine));
    EXPECT_EQ(m_tracker.completed_upto(), 200);
}

TEST_F(StreamTrackerTest, ForceRealloc) {
    static std::random_device s_rd{};
    static std::default_random_engine s_engine{s_rd()};
    std::uniform_int_distribution< int64_t > gen{0, 999};

    auto prev_size = get_mem_size();
    auto far_idx = (int64_t)StreamTracker< TestData >::alloc_blk_size + 1;
    m_tracker.create_and_complete(far_idx, gen(s_engine));
    // EXPECT_EQ(m_tracker.completed_upto(), -1);
    EXPECT_EQ(get_mem_size(), (prev_size * 2));

    for (int64_t i = 0; i < far_idx; ++i) {
        m_tracker.create_and_complete(i, gen(s_engine));
    }
    EXPECT_EQ(m_tracker.completed_upto(), far_idx);
    EXPECT_EQ(get_mem_size(), prev_size * 2);
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    auto ret = RUN_ALL_TESTS();
    return ret;
}
