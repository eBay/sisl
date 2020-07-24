#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "metrics.hpp"
#include <gtest/gtest.h>
#include <sds_logging/logging.h>

#define ITERATIONS 3

THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;
SDS_LOGGING_INIT(vmod_metrics_framework)

using namespace sisl;

void userA() {
    auto mgroup = std::make_shared< ThreadBufferMetricsGroup >("Group1", "Instance1");
    mgroup->register_counter("counter1", "Counter1");
    mgroup->register_counter("counter2", "Counter2");
    mgroup->register_counter("counter3", "Counter3");
    MetricsFarm::getInstance().register_metrics_group(mgroup);

    mgroup->counter_increment(0);
    mgroup->counter_increment(2, 4);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    mgroup->counter_increment(1);
    std::this_thread::sleep_for(std::chrono::seconds(4));
    MetricsFarm::getInstance().deregister_metrics_group(mgroup);
}

void userB() {
    auto mgroup = std::make_shared< ThreadBufferMetricsGroup >("Group2", "Instance1");
    mgroup->register_gauge("gauge1", "Gauge1");
    mgroup->register_gauge("gauge2", "Gauge2");
    MetricsFarm::getInstance().register_metrics_group(mgroup);

    mgroup->gauge_update(0, 5);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    mgroup->gauge_update(1, 2);
    mgroup->gauge_update(0, 3);
    std::this_thread::sleep_for(std::chrono::seconds(4));
    MetricsFarm::getInstance().deregister_metrics_group(mgroup);
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

uint64_t delay[ITERATIONS] = {2, 3, 4};

void gather() {
    for (auto i = 0U; i < ITERATIONS; i++) {
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

TEST(farmTest, gather) {
    std::thread th1(userA);
    std::thread th2(userB);
    std::thread th3(gather);

    th1.join();
    th2.join();
    th3.join();
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
