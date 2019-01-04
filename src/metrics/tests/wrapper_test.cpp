//
// Created by Kadayam, Hari on 12/12/18.
//

#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "metrics.hpp"
#include <gtest/gtest.h>

#define ITERATIONS 4

THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

using namespace sisl;

class TreeMetrics : public MetricsGroupWrapper {
public:
    explicit TreeMetrics(const char *grp_name) : MetricsGroupWrapper(grp_name ){
        REGISTER_COUNTER(tree_node_count, "tree_node_count", "");
        REGISTER_COUNTER(tree_obj_count, "tree_obj_count", "");
        REGISTER_COUNTER(tree_txns, "tree_txns", "");

        register_me_to_farm();
    }
};

class CacheMetrics : public MetricsGroupWrapper {
public:
    explicit CacheMetrics(const char *grp_name) : MetricsGroupWrapper(grp_name) {
        REGISTER_GAUGE(cache_size, "cache_size", "");
        REGISTER_GAUGE(cache_eviction_pct, "cache_eviction_pct", "");
        REGISTER_GAUGE(cache_writes_rate, "cache_writes_rate", "");

        REGISTER_HISTOGRAM(cache_write_latency, "cache_write_latency", "");
        REGISTER_HISTOGRAM(cache_read_latency, "cache_read_latency", "");
        REGISTER_HISTOGRAM(cache_delete_latency, "cache_delete_latency", "");

        register_me_to_farm();
    }
};

class Tree {
private:
    TreeMetrics m_metrics;

public:
    Tree(const char *grp_name) : m_metrics(grp_name) {}
    void update() {
        COUNTER_INCREMENT(m_metrics, tree_node_count, 1);
        COUNTER_INCREMENT(m_metrics, tree_obj_count, 4);
        COUNTER_INCREMENT(m_metrics, tree_obj_count, 8);
        COUNTER_INCREMENT(m_metrics, tree_txns, 2);
    }
};

class Cache {
private:
    CacheMetrics m_metrics;

public:
    Cache(const char *grp_name) : m_metrics(grp_name) {}
    void update() {
        GAUGE_UPDATE(m_metrics, cache_size, 1);
        GAUGE_UPDATE(m_metrics, cache_size, 4);
        GAUGE_UPDATE(m_metrics, cache_eviction_pct, 8);
        GAUGE_UPDATE(m_metrics, cache_writes_rate, 2);

        HISTOGRAM_OBSERVE(m_metrics, cache_write_latency, 100);
        HISTOGRAM_OBSERVE(m_metrics, cache_write_latency, 150);
        HISTOGRAM_OBSERVE(m_metrics, cache_read_latency, 150);
        HISTOGRAM_OBSERVE(m_metrics, cache_delete_latency, 200);
    }
};

TEST(counterTest, wrapperTest) {
    Tree tree1("tree1"), tree2("tree2");
    tree1.update();
    tree2.update();

    Cache cache1("cache1"), cache2("cache2");
    cache1.update();
    cache2.update();

    auto output = MetricsFarm::getInstance().getResultInJSONString();
    std::cout << "Output of gather = " << output << "\n";
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
