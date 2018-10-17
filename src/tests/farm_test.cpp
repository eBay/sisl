#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "include/metrics.hpp"
#include "include/thread_buffer.hpp"
#include "include/urcu_helper.hpp"
#include <gtest/gtest.h>

#define ITERATIONS 4

THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

void userA () {
    auto factory = std::make_shared<metrics::MetricsFactory>();
    metrics::MetricsFarm::getInstance()->registerFactory(factory);

    factory->registerCounter( "counter1", " for test", "" );
    factory->registerCounter( "counter2", " for test", "" );
    factory->registerCounter( "counter3", " for test", "" );

    factory->getCounter(0).increment();
    factory->getCounter(2).increment(4);
    std::this_thread::sleep_for (std::chrono::seconds(3));
    factory->getCounter(1).increment();
    std::this_thread::sleep_for (std::chrono::seconds(2));
    metrics::MetricsFarm::getInstance()->deregisterFactory(factory);
}

void userB () {
    std::this_thread::sleep_for (std::chrono::seconds(3));
    auto factory = std::make_shared<metrics::MetricsFactory>();
    metrics::MetricsFarm::getInstance()->registerFactory(factory);

    factory->registerGauge( "gauge1", " for test", "" );
    factory->registerGauge( "gauge2", " for test", "" );
    factory->getGauge(0).update(5);
    std::this_thread::sleep_for (std::chrono::seconds(3));
    factory->getGauge(1).update(2);
    factory->getGauge(0).update(3);
    std::this_thread::sleep_for (std::chrono::seconds(2));
    metrics::MetricsFarm::getInstance()->deregisterFactory(factory);
}

std::string expected[ITERATIONS] = {
    "{\"Counters\":{\"counter1 for test\":1,\"counter2 for test\":0,\
        \"counter3 for test\":4},\"Gauges\":null,\"Histograms percentiles \
        (usecs) avg/50/95/99\":null}",
    "{\"Counters\":{\"counter1 for test\":1,\"counter2 for test\":1,\
        \"counter3 for test\":4},\"Gauges\":{\"gauge1 for test\":5,\
        \"gauge2 for test\":0},\"Histograms percentiles (usecs) \
        avg/50/95/99\":null}",
    "{\"Counters\":null,\"Gauges\":{\"gauge1 for test\":3,\
        \"gauge2 for test\":2},\"Histograms percentiles (usecs) \
        avg/50/95/99\":null}",
    "{\"Counters\":null,\"Gauges\":null,\"Histograms percentiles (usecs) \
        avg/50/95/99\":null}"
};

uint64_t delay[ITERATIONS] = {2, 2, 3, 3};

void gather () {
    for (auto i = 0U; i < ITERATIONS; i++) {
        std::this_thread::sleep_for (std::chrono::seconds(delay[i]));
        auto output = metrics::MetricsFarm::getInstance()->gather();
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
