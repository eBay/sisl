#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "metrics.hpp"

#define ITERATIONS 6

class TestMetrics {
public:
    TestMetrics() {
        m_report_metrics = new metrics::ReportMetrics();
    }

    ~TestMetrics() {
        delete m_report_metrics;
    }

    metrics::ReportMetrics* getMetrics() { return m_report_metrics; }

private:
    metrics::ReportMetrics* m_report_metrics;
};

void seqA (TestMetrics *report) {
    auto c1_addr = report->getMetrics()->getCounter(0);
    auto c2_addr = report->getMetrics()->getCounter(1);
    auto g1_addr = report->getMetrics()->getGauge(0);

    std::this_thread::sleep_for (std::chrono::seconds(1));
    c1_addr->increment();
    std::this_thread::sleep_for (std::chrono::seconds(1));
    c2_addr->increment();
    c2_addr->decrement();
    g1_addr->update(2);
}

void seqB (TestMetrics *report) {
    auto c1_addr = report->getMetrics()->getCounter(0);
    auto c2_addr = report->getMetrics()->getCounter(1);
    auto g1_addr = report->getMetrics()->getGauge(0);

    c1_addr->increment();
    c2_addr->increment();
    std::this_thread::sleep_for (std::chrono::seconds(1));
    c2_addr->decrement();
    std::this_thread::sleep_for (std::chrono::seconds(1));
    g1_addr->update(5);
}

void gather (TestMetrics *report) {
    for (auto i = 0U; i < ITERATIONS; i++) {
        report->getMetrics()->gather();
        std::string filename = "iter" + std::to_string(i) +".json";
        std::ofstream ofs (filename, std::ofstream::out);
        ofs << report->getMetrics()->getJSON();
        ofs.close();
        std::this_thread::sleep_for (std::chrono::seconds(1));
    }
}

int main() {
    TestMetrics *report = new TestMetrics();
    auto c1 = report->getMetrics()->registerCounter( "counter1", "counter1 for test", "", 1 );
    auto g1 = report->getMetrics()->registerGauge( "gauge1", "gauge1 for test", "", 3 );
    auto c2 = report->getMetrics()->registerCounter( "counter2", "counter2 for test", "", 10 );

    std::thread th1 (seqA, report);
    std::thread th2 (seqB, report);
    std::thread th3 (gather, report);

    th1.join();
    th2.join();
    th3.join();

    delete report;
}
