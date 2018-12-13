#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "include/metrics.hpp"
#include "include/thread_buffer.hpp"
#include "include/urcu_helper.hpp"
#include <gtest/gtest.h>

#define ITERATIONS 2

//CREATE_REPORT;
THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

using namespace sisl;
using namespace sisl::metrics;

MetricsGroupPtr glob_mgroup;

void seqA () {
    std::this_thread::sleep_for (std::chrono::seconds(1));
    glob_mgroup->counterIncrement(0);
    glob_mgroup->histogramObserve(0, 2);
    glob_mgroup->histogramObserve(0, 5);

    std::this_thread::sleep_for (std::chrono::seconds(2));

    glob_mgroup->histogramObserve(0, 5);
    glob_mgroup->counterIncrement(1);
    glob_mgroup->gaugeUpdate(0, 2);
}

void seqB () {
    glob_mgroup->counterIncrement(0);
    glob_mgroup->counterIncrement(1);

    std::this_thread::sleep_for (std::chrono::seconds(3));

    glob_mgroup->counterDecrement(0, 2);
    glob_mgroup->counterDecrement(1);

    std::this_thread::sleep_for (std::chrono::seconds(1));

    glob_mgroup->gaugeUpdate(0, 5);
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
            })result"
};

uint64_t delay[ITERATIONS] = {2,4};

void gather () {
    for (auto i = 0U; i < ITERATIONS; i++) {
        std::this_thread::sleep_for (std::chrono::seconds(delay[i]));
        auto output = metrics::MetricsFarm::getInstance().getResultInJSONString();
        output.erase( std::remove_if( output.begin(), output.end(),
                    [l = std::locale{}](auto ch) { return std::isspace(ch, l); }),
                output.end());
        expected[i].erase( std::remove_if( expected[i].begin(), expected[i].end(),
                    [l = std::locale{}](auto ch) { return std::isspace(ch, l); }),
                expected[i].end());
        EXPECT_EQ( output, expected[i] );
    }
}

TEST(functionalityTest, gather) {
    std::thread th1 (seqA);
    std::thread th2 (seqB);
    std::thread th3 (gather);

    th1.join();
    th2.join();
    th3.join();
}

int main(int argc, char* argv[]) {
    glob_mgroup = metrics::MetricsGroup::make_group();

    glob_mgroup->registerCounter( "counter1", "Counter1", "" );
    glob_mgroup->registerCounter( "counter2", "Counter2", "" );
    glob_mgroup->registerCounter( "counter3", "Counter3", "" );

    glob_mgroup->registerGauge( "gauge1", "Gauge1", "" );
    glob_mgroup->registerGauge( "gauge2", "Gauge2", "" );

    glob_mgroup->registerHistogram( "hist", "Histogram1", "" );

    metrics::MetricsFarm::getInstance().registerMetricsGroup(glob_mgroup);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
