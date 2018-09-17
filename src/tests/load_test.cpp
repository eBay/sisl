#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "include/metrics.hpp"
#include "include/thread_buffer.hpp"
#include "include/urcu_helper.hpp"

#define ITERATIONS 10

CREATE_REPORT;
THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

void seqA () {
    REPORT.getCounter(0)->increment();

    std::this_thread::sleep_for (std::chrono::seconds(2));

    REPORT.getCounter(0)->increment();
    REPORT.getCounter(0)->increment();
    REPORT.getCounter(0)->increment();
    REPORT.getCounter(8)->increment(3);
    REPORT.getHistogram(0)->update(2);
    REPORT.getHistogram(0)->update(5);

    std::this_thread::sleep_for (std::chrono::seconds(2));

    REPORT.getCounter(0)->increment();
    REPORT.getCounter(0)->increment();
    REPORT.getCounter(7)->increment(2);
    REPORT.getHistogram(0)->update(5);
    REPORT.getCounter(1)->increment();
    REPORT.getGauge(0)->update(2);
}

void seqB () {
    REPORT.getCounter(0)->increment();
    REPORT.getCounter(0)->increment(2);
    REPORT.getCounter(1)->increment();

    std::this_thread::sleep_for (std::chrono::seconds(1));

    REPORT.getCounter(1)->increment();
    REPORT.getCounter(0)->decrement(2);
    REPORT.getCounter(1)->decrement();

    std::this_thread::sleep_for (std::chrono::seconds(3));

    REPORT.getGauge(0)->update(5);
}

void gather () {
    std::string filename = "result.json";
    std::ofstream ofs (filename, std::ofstream::out);
    for (auto i = 0U; i < ITERATIONS; i++) {
        ofs << REPORT.gather()->getJSON() << std::endl;
        std::this_thread::sleep_for (std::chrono::seconds(1));
    }
    ofs.close();
}

int main() {
    REPORT.registerCounter( "counter1", "counter1 for test", "" );
    REPORT.registerCounter( "counter2", "counter2 for test", "" );
    REPORT.registerCounter( "counter3", "counter3 for test", "" );
    REPORT.registerCounter( "counter4", "counter4 for test", "" );
    REPORT.registerCounter( "counter5", "counter5 for test", "" );
    REPORT.registerCounter( "counter6", "counter6 for test", "" );
    REPORT.registerCounter( "counter7", "counter7 for test", "" );
    REPORT.registerCounter( "counter8", "counter8 for test", "" );
    REPORT.registerCounter( "counter9", "counter9 for test", "" );

    REPORT.registerGauge( "gauge1", "gauge1 for test", "" );
    REPORT.registerGauge( "gauge2", "gauge2 for test", "" );

    REPORT.registerHistogram( "hist", "histogram for test", "" );

    std::thread th1 (seqA);
    std::thread th2 (seqB);
    std::thread th3 (gather);

    th1.join();
    th2.join();
    th3.join();
}
