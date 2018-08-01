#include "metrics.hpp"
#include <fstream>

class TestMetrics {
public:
    TestMetrics() = default;
    metrics::ReportMetrics getMetrics() { return m_report_metrics; }

private:
    static metrics::ReportMetrics m_report_metrics;
};

int main() {
    TestMetrics report;
    auto a_cindx = report.getMetrics().registerCounter( "counter1", "counter for test1", "", 0 );
    auto a_gindx = report.getMetrics().registerGauge( "gauge1", "gauge for test2", "", 3 );

    report.getMetrics().getCounter(a_cindx).increment();
    report.getMetrics().getCounter(a_cindx).increment();

    report.getMetrics().getGauge(a_gindx).update(2);

    report.getMetrics().getCounter(a_cindx).decrement();

    report.getMetrics().gather();

    std::ofstream ofs ("test.txt", std::ofstream::out);
    ofs << report.getMetrics().getJSON();
    ofs.close();

    return 0;
}
