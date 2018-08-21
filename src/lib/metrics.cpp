/* Author: Sounak Gupta, July/Aug 2018 */

#include "include/metrics.hpp"

namespace metrics {

ReportMetrics* ReportMetrics::m_instance = 0;

ReportMetrics* ReportMetrics::getInstance() {
    if (!ReportMetrics::m_instance) {
        ReportMetrics::m_instance = new ReportMetrics();
    }
    return ReportMetrics::m_instance;
}

void ReportMetrics::deleteInstance() {
    delete ReportMetrics::m_instance;
}

}
