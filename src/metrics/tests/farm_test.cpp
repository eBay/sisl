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
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

#include <gtest/gtest.h>
#include <sisl/logging/logging.h>

#include "sisl/metrics/metrics.hpp"

constexpr size_t ITERATIONS{3};

RCU_REGISTER_INIT
SISL_LOGGING_INIT(vmod_metrics_framework)

using namespace sisl;

class Group1Metrics : public MetricsGroup {
public:
    explicit Group1Metrics(const char* inst_name)
        : MetricsGroup("Group1", inst_name, group_impl_type_t::thread_buf_signal) {
        REGISTER_COUNTER(counter1, "Counter1");
        REGISTER_COUNTER(counter2, "Counter2");
        REGISTER_COUNTER(counter3, "Counter3");
        register_me_to_farm();
    }
    ~Group1Metrics() { deregister_me_from_farm(); }
};

class Group2Metrics : public MetricsGroup {
public:
    explicit Group2Metrics(const char* inst_name)
        : MetricsGroup("Group2", inst_name, group_impl_type_t::thread_buf_signal) {
        REGISTER_GAUGE(gauge1, "Gauge1");
        REGISTER_GAUGE(gauge2, "Gauge2");
        register_me_to_farm();
    }
    ~Group2Metrics() { deregister_me_from_farm(); }
};

void userA() {
    Group1Metrics metrics("Instance1");

    COUNTER_INCREMENT(metrics, counter1, 1);
    COUNTER_INCREMENT(metrics, counter3, 4);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    COUNTER_INCREMENT(metrics, counter2, 1);
    std::this_thread::sleep_for(std::chrono::seconds(4));
}

void userB() {
    Group2Metrics metrics("Instance1");

    GAUGE_UPDATE(metrics, gauge1, 5);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    GAUGE_UPDATE(metrics, gauge2, 2);
    GAUGE_UPDATE(metrics, gauge1, 3);
    std::this_thread::sleep_for(std::chrono::seconds(4));
}

// clang-format off
nlohmann::json expected[ITERATIONS] = {
        {
            {"Group1", {
                {"Instance1", {
                    {"Counters", {
                        {"Counter1", 1},
                        {"Counter2", 0},
                        {"Counter3", 4}
                    }},
                    {"Gauges", {}},
                    {"Histograms percentiles (usecs) avg/50/95/99", {}}
                }},
            }},
            {"Group2", {
               {"Instance1", {
                    {"Counters", {}},
                    {"Gauges", {
                        {"Gauge1", 5},
                        {"Gauge2", 0}
                    }},
                    {"Histograms percentiles (usecs) avg/50/95/99", {}}
                }}
            }}
        },
        {
            {"Group1", {
                {"Instance1", {
                    {"Counters", {
                        {"Counter1", 1},
                        {"Counter2", 1},
                        {"Counter3", 4}
                    }},
                    {"Gauges", {}},
                    {"Histograms percentiles (usecs) avg/50/95/99", {}}
                }}
            }},
            {"Group2", {
                {"Instance1", {
                    {"Counters", {}},
                    {"Gauges", {
                       {"Gauge1", 3},
                       {"Gauge2", 2}
                    }},
                    {"Histograms percentiles (usecs) avg/50/95/99", {}}
                }}
            }},
        },
        {
        },
};
// clang-format on

std::array< uint64_t, ITERATIONS > delay{2, 3, 4};

void gather() {
    for (size_t i{0}; i < ITERATIONS; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(delay[i]));
        auto output = MetricsFarm::getInstance().get_result_in_json();

        nlohmann::json patch = nlohmann::json::diff(output, expected[i]);
        EXPECT_EQ(patch.empty(), true);
        if (!patch.empty()) {
            std::cerr << "On Iteration " << i << "\n";
            std::cerr << "Actual     " << std::setw(4) << output << "\n";
            std::cerr << "Expected   " << std::setw(4) << expected[i] << "\n";
            std::cerr << "Diff patch " << std::setw(4) << patch << "\n";
        }
    }
}

TEST(FarmTest, gather) {
    std::thread th1(userA);
    std::thread th2(userB);
    std::thread th3(gather);

    th1.join();
    th2.join();
    th3.join();
}

// Helper class for DirectAccess tests
class TestMetrics : public MetricsGroup {
public:
    explicit TestMetrics(const char* inst_name, group_impl_type_t type = group_impl_type_t::rcu)
        : MetricsGroup("TestGroup", inst_name, type) {
        REGISTER_COUNTER(test_counter, "Test counter");
        REGISTER_GAUGE(test_gauge, "Test gauge");
        REGISTER_HISTOGRAM(test_histogram, "Test histogram");
        register_me_to_farm();
    }
    ~TestMetrics() { deregister_me_from_farm(); }
};

// Parameterized test fixture for DirectAccess tests
class DirectAccessTest : public ::testing::TestWithParam< group_impl_type_t > {};

// Test direct access to counter, gauge, and histogram values
TEST_P(DirectAccessTest, allMetricTypes) {
    group_impl_type_t impl_type = GetParam();
    TestMetrics metrics("direct_access_test", impl_type);

    // Test counter
    COUNTER_INCREMENT(metrics, test_counter, 5);
    COUNTER_INCREMENT(metrics, test_counter, 10);
    COUNTER_INCREMENT(metrics, test_counter, 15);
    int64_t counter_value = COUNTER_VALUE(metrics, test_counter);
    EXPECT_EQ(counter_value, 30);

    // Test gauge
    GAUGE_UPDATE(metrics, test_gauge, 100);
    EXPECT_EQ(GAUGE_VALUE(metrics, test_gauge), 100);
    GAUGE_UPDATE(metrics, test_gauge, 250);
    EXPECT_EQ(GAUGE_VALUE(metrics, test_gauge), 250);

    // Test histogram
    HISTOGRAM_OBSERVE(metrics, test_histogram, 10);
    HISTOGRAM_OBSERVE(metrics, test_histogram, 20);
    HISTOGRAM_OBSERVE(metrics, test_histogram, 30);
    HISTOGRAM_OBSERVE(metrics, test_histogram, 40);
    HISTOGRAM_OBSERVE(metrics, test_histogram, 50);

    sisl::HistogramStatistics stats = HISTOGRAM_VALUE(metrics, test_histogram);
    EXPECT_EQ(stats.count, 5);
    EXPECT_DOUBLE_EQ(stats.average, 30.0);
    EXPECT_GT(stats.p50, 0.0);
    EXPECT_GT(stats.p95, 0.0);
    EXPECT_GT(stats.p99, 0.0);
}

INSTANTIATE_TEST_SUITE_P(AllImplementations, DirectAccessTest,
                         ::testing::Values(group_impl_type_t::rcu, group_impl_type_t::atomic,
                                           group_impl_type_t::thread_buf_signal),
                         [](const ::testing::TestParamInfo< group_impl_type_t >& info) {
                             switch (info.param) {
                             case group_impl_type_t::rcu: return "RCU";
                             case group_impl_type_t::atomic: return "Atomic";
                             case group_impl_type_t::thread_buf_signal: return "ThreadLocal";
                             default: return "Unknown";
                             }
                         });

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
