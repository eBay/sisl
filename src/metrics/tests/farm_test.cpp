#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "metrics.hpp"
#include <gtest/gtest.h>

#define ITERATIONS 4

THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

using namespace sisl;

void userA () {
    auto mgroup = std::make_shared<MetricsGroup>();
    mgroup->register_counter("counter1", "Counter1", "");
    mgroup->register_counter("counter2", "Counter2", "");
    mgroup->register_counter("counter3", "Counter3", "");

    MetricsFarm::getInstance().register_metrics_group(mgroup);

    mgroup->counter_increment(0);
    mgroup->counter_increment(2, 4);
    std::this_thread::sleep_for (std::chrono::seconds(3));
    mgroup->counter_increment(1);
    std::this_thread::sleep_for (std::chrono::seconds(2));
    MetricsFarm::getInstance().deregister_metrics_group(mgroup);
}

void userB () {
    std::this_thread::sleep_for (std::chrono::seconds(3));

    auto mgroup = std::make_shared<MetricsGroup>();
    mgroup->register_gauge("gauge1", "Gauge1", "");
    mgroup->register_gauge("gauge2", "Gauge2", "");
    MetricsFarm::getInstance().register_metrics_group(mgroup);

    mgroup->gauge_update(0, 5);
    std::this_thread::sleep_for (std::chrono::seconds(3));
    mgroup->gauge_update(1, 2);
    mgroup->gauge_update(0, 3);
    std::this_thread::sleep_for (std::chrono::seconds(2));
    MetricsFarm::getInstance().deregister_metrics_group(mgroup);
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
        auto output = MetricsFarm::getInstance().get_result_in_json_string();
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
