#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "include/metrics.hpp"

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
    assert(report->getMetrics()->getCounter(2));

    auto g1_addr = report->getMetrics()->getGauge(0);
    assert(report->getMetrics()->getGauge(1));

    auto h_addr = report->getMetrics()->getHistogram(0);

    std::this_thread::sleep_for (std::chrono::seconds(2));

    c1_addr->increment();
    h_addr->update(2);
    h_addr->update(5);

    std::this_thread::sleep_for (std::chrono::seconds(2));

    h_addr->update(5);
    c2_addr->increment();
    g1_addr->update(2);
}

void seqB (TestMetrics *report) {
    auto c1_addr = report->getMetrics()->getCounter(0);
    auto c2_addr = report->getMetrics()->getCounter(1);
    assert(report->getMetrics()->getCounter(2));

    auto g1_addr = report->getMetrics()->getGauge(0);
    assert(report->getMetrics()->getGauge(1));

    assert(report->getMetrics()->getHistogram(0));

    c1_addr->increment();
    c2_addr->increment();

    std::this_thread::sleep_for (std::chrono::seconds(1));

    c1_addr->decrement(2);
    c2_addr->decrement();

    std::this_thread::sleep_for (std::chrono::seconds(3));

    g1_addr->update(5);
}

void gather (TestMetrics *report) {
    std::string filename = "result.json";
    std::ofstream ofs (filename, std::ofstream::out);
    for (auto i = 0U; i < ITERATIONS; i++) {
        report->getMetrics()->gather();
        ofs << report->getMetrics()->getJSON() << std::endl;
        std::this_thread::sleep_for (std::chrono::seconds(1));
    }
    ofs.close();
}

int main() {
    TestMetrics *report = new TestMetrics();

    report->getMetrics()->registerCounter( "counter1", "counter1 for test", "", 5 );
    report->getMetrics()->registerCounter( "counter2", "counter2 for test", "", -2 );
    report->getMetrics()->registerCounter( "counter3", "counter3 for test", "", 0 );

    report->getMetrics()->registerGauge( "gauge1", "gauge1 for test", "", 3 );
    report->getMetrics()->registerGauge( "gauge2", "gauge2 for test", "", -2 );

    report->getMetrics()->registerHistogram( "hist", "histogram for test", "");

    std::thread th1 (seqA, report);
    std::thread th2 (seqB, report);
    std::thread th3 (gather, report);

    th1.join();
    th2.join();
    th3.join();

    delete report;
}
