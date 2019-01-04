#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "include/metrics.hpp"
#include <gtest/gtest.h>

#define ITERATIONS 4

THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

using namespace sisl;

void userA () {
    auto mgroup = std::make_shared<metrics::MetricsGroup>();
    mgroup->registerCounter( "counter1", "Counter1", "" );
    mgroup->registerCounter( "counter2", "Counter2", "" );
    mgroup->registerCounter( "counter3", "Counter3", "" );

    metrics::MetricsFarm::getInstance().registerMetricsGroup(mgroup);

    mgroup->counterIncrement(0);
    mgroup->counterIncrement(2, 4);
    std::this_thread::sleep_for (std::chrono::seconds(3));
    mgroup->counterIncrement(1);
    std::this_thread::sleep_for (std::chrono::seconds(2));
    metrics::MetricsFarm::getInstance().deregisterMetricsGroup(mgroup);
}

void userB () {
    std::this_thread::sleep_for (std::chrono::seconds(3));

    auto mgroup = std::make_shared<metrics::MetricsGroup>();
    mgroup->registerGauge( "gauge1", "Gauge1", "" );
    mgroup->registerGauge( "gauge2", "Gauge2", "" );
    metrics::MetricsFarm::getInstance().registerMetricsGroup(mgroup);

    mgroup->gaugeUpdate(0, 5);
    std::this_thread::sleep_for (std::chrono::seconds(3));
    mgroup->gaugeUpdate(1, 2);
    mgroup->gaugeUpdate(0, 3);
    std::this_thread::sleep_for (std::chrono::seconds(2));
    metrics::MetricsFarm::getInstance().deregisterMetricsGroup(mgroup);
}

std::string expected[ITERATIONS] = {
    R"result({"metrics_group_0":{"Counters":{"Counter1":1,"Counter2":0,"Counter3":4},"Gauges":null,"Histogramspercentiles(usecs)avg/50/95/99":null}})result",
    R"result({"metrics_group_0":{"Counters":{"Counter1":1,"Counter2":1,"Counter3":4},"Gauges":null,"Histogramspercentiles(usecs)avg/50/95/99":null}})result",
    R"result({"metrics_group_0":{"Counters":null,"Gauges":{"Gauge1":3,"Gauge2":2},"Histogramspercentiles(usecs)avg/50/95/99":null}})result",
    "null"
};

uint64_t delay[ITERATIONS] = {2, 2, 3, 3};

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

TEST(farmTest, gather) {
    std::thread th1 (userA);
    std::thread th2 (userB);
    std::thread th3 (gather);

    th1.join();
    th2.join();
    th3.join();
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
