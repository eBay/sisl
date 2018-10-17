#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "include/metrics.hpp"
#include "include/thread_buffer.hpp"
#include "include/urcu_helper.hpp"
#include <gtest/gtest.h>

#define ITERATIONS 2

CREATE_REPORT;
THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

void seqA () {
    std::this_thread::sleep_for (std::chrono::seconds(1));
    REPORT.getCounter(0).increment();
    REPORT.getHistogram(0).update(2);
    REPORT.getHistogram(0).update(5);

    std::this_thread::sleep_for (std::chrono::seconds(2));

    REPORT.getHistogram(0).update(5);
    REPORT.getCounter(1).increment();
    REPORT.getGauge(0).update(2);
}

void seqB () {
    REPORT.getCounter(0).increment();
    REPORT.getCounter(1).increment();

    std::this_thread::sleep_for (std::chrono::seconds(3));

    REPORT.getCounter(0).decrement(2);
    REPORT.getCounter(1).decrement();

    std::this_thread::sleep_for (std::chrono::seconds(1));

    REPORT.getGauge(0).update(5);
}

std::string expected[ITERATIONS] = {
    "{\"Counters\":{\"counter1 for test\":2,\"counter2 for test\":1,\
        \"counter3 for test\":0},\"Gauges\":{\"gauge1 for test\":0,\
        \"gauge2 for test\":0},\"Histograms percentiles (usecs) \
        avg/50/95/99\":{\"hist for test\":\"3 / 0 / 0 / 0\"}}",
    "{\"Counters\":{\"counter1 for test\":0,\"counter2 for test\":1,\
        \"counter3 for test\":0},\"Gauges\":{\"gauge1 for test\":5,\
        \"gauge2 for test\":0},\"Histograms percentiles (usecs) \
        avg/50/95/99\":{\"hist for test\":\"4 / 0 / 0 / 0\"}}"
};

uint64_t delay[ITERATIONS] = {2,4};

void gather () {
    for (auto i = 0U; i < ITERATIONS; i++) {
        std::this_thread::sleep_for (std::chrono::seconds(delay[i]));
        auto output = REPORT.gather()->getJSON();
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
    REPORT.registerCounter( "counter1", " for test", "" );
    REPORT.registerCounter( "counter2", " for test", "" );
    REPORT.registerCounter( "counter3", " for test", "" );

    REPORT.registerGauge( "gauge1", " for test", "" );
    REPORT.registerGauge( "gauge2", " for test", "" );

    REPORT.registerHistogram( "hist", " for test", "" );

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
