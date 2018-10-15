#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "include/metrics.hpp"
#include "include/thread_buffer.hpp"
#include "include/urcu_helper.hpp"

#define ITERATIONS 10

THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

void userA () {
    metrics::MetricsFactory factory;
    metrics::MetricsFarm::getInstance()->registerFactory(&factory);

    factory.registerCounter( "counter1", " for test", "" );
    factory.registerCounter( "counter2", " for test", "" );
    factory.registerCounter( "counter3", " for test", "" );

    std::this_thread::sleep_for (std::chrono::seconds(2));
    factory.getCounter(0)->increment();
    factory.getCounter(2)->increment(4);
    std::this_thread::sleep_for (std::chrono::seconds(2));
    factory.getCounter(1)->increment();

    std::this_thread::sleep_for (std::chrono::seconds(2));
    metrics::MetricsFarm::getInstance()->deregisterFactory(&factory);
}

void userB () {
    std::this_thread::sleep_for (std::chrono::seconds(3));

    metrics::MetricsFactory factory;
    metrics::MetricsFarm::getInstance()->registerFactory(&factory);

    factory.registerGauge( "gauge1", " for test", "" );
    factory.registerGauge( "gauge2", " for test", "" );

    factory.getGauge(0)->update(5);
    std::this_thread::sleep_for (std::chrono::seconds(1));
    factory.getGauge(1)->update(2);
    std::this_thread::sleep_for (std::chrono::seconds(3));
    factory.getGauge(0)->update(3);

    std::this_thread::sleep_for (std::chrono::seconds(1));
    metrics::MetricsFarm::getInstance()->deregisterFactory(&factory);
}

void gather () {
    std::string filename = "result.json";
    std::ofstream ofs (filename, std::ofstream::out);
    for (auto i = 0U; i < ITERATIONS; i++) {
        ofs << metrics::MetricsFarm::getInstance()->gather() << std::endl;
        std::this_thread::sleep_for (std::chrono::seconds(1));
    }
    ofs.close();
}

int main() {

    std::thread th1 (userA);
    std::thread th2 (userB);
    std::thread th3 (gather);

    th1.join();
    th2.join();
    th3.join();
}
