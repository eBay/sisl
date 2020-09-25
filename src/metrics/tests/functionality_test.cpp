#include <array>
#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include <gtest/gtest.h>
#include <sds_logging/logging.h>

#include "../metrics.hpp"
#include "../metrics_group_impl.hpp"


constexpr size_t ITERATIONS{2};

// CREATE_REPORT;
THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;
SDS_LOGGING_INIT(vmod_metrics_framework)

using namespace sisl;

MetricsGroupImplPtr glob_mgroup;

void seqA() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    glob_mgroup->counter_increment(0, 1);
    glob_mgroup->histogram_observe(0, 2);
    glob_mgroup->histogram_observe(0, 5);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    glob_mgroup->histogram_observe(0, 5);
    glob_mgroup->counter_increment(1, 1);
    glob_mgroup->gauge_update(0, 2);
}

void seqB() {
    glob_mgroup->counter_increment(0, 1);
    glob_mgroup->counter_increment(1, 1);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    glob_mgroup->counter_decrement(0, 2);
    glob_mgroup->counter_decrement(1);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    glob_mgroup->gauge_update(0, 5);
}

std::string expected[ITERATIONS] = {
    R"result({"metrics_group_0": {
                "Counters":{"Counter1":2,"Counter2":1,"Counter3":0},
                "Gauges":{"Gauge1":0,"Gauge2":0},
                "Histogramspercentiles(usecs)avg/50/95/99":{"Histogram1":"3/0/0/0"}
                }
            })result",
    R"result({"metrics_group_0":{
                "Counters":{"Counter1":0,"Counter2":1,"Counter3":0},
                "Gauges":{"Gauge1":5,"Gauge2":0},
                "Histogramspercentiles(usecs)avg/50/95/99":{"Histogram1":"4/0/0/0"}
                }
            })result"};

std::array<uint64_t, ITERATIONS> delay{2, 4};

void gather() {
    for (size_t i{0}; i < ITERATIONS; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(delay[i]));
        auto output = MetricsFarm::getInstance().get_result_in_json_string();
        output.erase(
            std::remove_if(output.begin(), output.end(), [l = std::locale{}](auto ch) { return std::isspace(ch, l); }),
            output.end());
        expected[i].erase(std::remove_if(expected[i].begin(), expected[i].end(),
                                         [l = std::locale{}](auto ch) { return std::isspace(ch, l); }),
                          expected[i].end());
        EXPECT_EQ(output, expected[i]);
    }
}

TEST(functionalityTest, gather) {
    std::thread th1(seqA);
    std::thread th2(seqB);
    std::thread th3(gather);

    th1.join();
    th2.join();
    th3.join();
}

int main(int argc, char* argv[]) {
    glob_mgroup = MetricsGroup::make_group();

    glob_mgroup->register_counter("counter1", "Counter1");
    glob_mgroup->register_counter("counter2", "Counter2");
    glob_mgroup->register_counter("counter3", "Counter3");

    glob_mgroup->register_gauge("gauge1", "Gauge1", "");
    glob_mgroup->register_gauge("gauge2", "Gauge2", "");

    glob_mgroup->register_histogram("hist", "Histogram1", "");

    MetricsFarm::getInstance().register_metrics_group(glob_mgroup);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
