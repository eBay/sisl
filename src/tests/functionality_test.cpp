#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "include/metrics.hpp"

#define ITERATIONS 6

CREATE_REPORT;

void seqA () {
    auto c1_addr = REPORT.getCounter(0);
    auto c2_addr = REPORT.getCounter(1);
    assert(REPORT.getCounter(2));

    auto g1_addr = REPORT.getGauge(0);
    assert(REPORT.getGauge(1));

    auto h_addr = REPORT.getHistogram(0);

    std::this_thread::sleep_for (std::chrono::seconds(2));

    c1_addr->increment();
    h_addr->update(2);
    h_addr->update(5);

    std::this_thread::sleep_for (std::chrono::seconds(2));

    h_addr->update(5);
    c2_addr->increment();
    g1_addr->update(2);
}

void seqB () {
    auto c1_addr = REPORT.getCounter(0);
    auto c2_addr = REPORT.getCounter(1);
    assert(REPORT.getCounter(2));

    auto g1_addr = REPORT.getGauge(0);
    assert(REPORT.getGauge(1));

    assert(REPORT.getHistogram(0));

    c1_addr->increment();
    c2_addr->increment();

    std::this_thread::sleep_for (std::chrono::seconds(1));

    c1_addr->decrement(2);
    c2_addr->decrement();

    std::this_thread::sleep_for (std::chrono::seconds(3));

    g1_addr->update(5);
}

void gather () {
    std::string filename = "result.json";
    std::ofstream ofs (filename, std::ofstream::out);
    for (auto i = 0U; i < ITERATIONS; i++) {
        REPORT.gather();
        ofs << REPORT.getJSON() << std::endl;
        std::this_thread::sleep_for (std::chrono::seconds(1));
    }
    ofs.close();
}

int main() {
    REPORT.registerCounter( "counter1", "counter1 for test", "", 5 );
    REPORT.registerCounter( "counter2", "counter2 for test", "", -2 );
    REPORT.registerCounter( "counter3", "counter3 for test", "", 0 );

    REPORT.registerGauge( "gauge1", "gauge1 for test", "", 3 );
    REPORT.registerGauge( "gauge2", "gauge2 for test", "", -2 );

    REPORT.registerHistogram( "hist", "histogram for test", "");

    std::thread th1 (seqA);
    std::thread th2 (seqB);
    std::thread th3 (gather);

    th1.join();
    th2.join();
    th3.join();
}
