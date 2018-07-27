#include "src/metrics.hpp"
#include <fstream>

int main() {
    metrics::ReportMetrics report;
    auto a_cindx = report.registerCounter("counter1", "counter for test1", "", 0);
    auto a_gindx = report.registerGauge("gauge1", "gauge for test2", "", 3);

    report.fetchCounter(a_cindx).increment();
    report.fetchCounter(a_cindx).increment();

    report.fetchCounter(a_gindx).increment();

    report.fetchCounter(a_cindx).increment();
    report.fetchCounter(a_cindx).decrement();

    report.gather();
    std::ofstream ofs ("test.txt", std::ofstream::out);
    ofs << report.get_json();
    ofs.close();

    return 0;
}
