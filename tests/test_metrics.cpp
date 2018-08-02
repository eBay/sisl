#include "metrics.hpp"
#include <fstream>

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

int main() {
    TestMetrics *report = new TestMetrics();
    auto a_cindx = report->getMetrics()->registerCounter( "counter1", "counter for test1", "", 0 );
    auto a_gindx = report->getMetrics()->registerGauge( "gauge1", "gauge for test2", "", 3 );

    report->getMetrics()->getCounter(a_cindx).increment();
    report->getMetrics()->getCounter(a_cindx).increment();

    report->getMetrics()->getGauge(a_gindx).update(2);

    report->getMetrics()->getCounter(a_cindx).decrement();

    report->getMetrics()->gather();

    std::ofstream ofs ("test.txt", std::ofstream::out);
    ofs << report->getMetrics()->getJSON();
    ofs.close();

    delete report;

    return 0;
}
