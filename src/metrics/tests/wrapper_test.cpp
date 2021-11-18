//
// Created by Kadayam, Hari on 12/12/18.
//

#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include "metrics.hpp"
#include <gtest/gtest.h>
#include <sds_options/options.h>

SISL_LOGGING_INIT(vmod_metrics_framework)

RCU_REGISTER_INIT

using namespace sisl;

class TreeMetrics : public MetricsGroupWrapper {
public:
    explicit TreeMetrics(const char* inst_name) : MetricsGroupWrapper("Tree", inst_name) {
        REGISTER_COUNTER(tree_node_count, "Total number of nodes in tree", "");
        REGISTER_COUNTER(tree_op_write_count, "Total number of write ops in tree", "tree_op_count",
                         {"op_type", "write"});
        REGISTER_COUNTER(tree_op_read_count, "Total number of read ops in tree", "tree_op_count", {"op_type", "read"});
        REGISTER_COUNTER(tree_obj_count, "Total tree object count");

        register_me_to_farm();
    }
};

class CacheMetrics : public MetricsGroupWrapper {
public:
    explicit CacheMetrics(const char* inst_name) : MetricsGroupWrapper("Cache", inst_name) {
        REGISTER_GAUGE(cache_size, "Total cache size");
        REGISTER_GAUGE(cache_eviction_pct, "Cache Eviction Percent");
        REGISTER_GAUGE(cache_writes_rate, "Cache Write rate", "");

        REGISTER_HISTOGRAM(cache_write_latency, "Cache Write Latency");
        REGISTER_HISTOGRAM(cache_read_latency, "Cache Read Latency");
        REGISTER_HISTOGRAM(cache_delete_latency, "Cache Delete Latency");

        register_me_to_farm();
    }
};

class GlobalMetrics : public MetricsGroupWrapper {
public:
    explicit GlobalMetrics() : MetricsGroupWrapper("Global") {
        REGISTER_COUNTER(num_open_connections, "Total number of connections", sisl::_publish_as::publish_as_gauge);
        REGISTER_GAUGE(mem_utilization, "Total memory utilization");
        REGISTER_HISTOGRAM(request_per_txn, "Distribution of request per transactions",
                           HistogramBucketsType(LinearUpto64Buckets));

        register_me_to_farm();
    }
};

class Tree {
private:
    TreeMetrics m_metrics;

public:
    Tree(const char* grp_name) : m_metrics(grp_name) {}
    void update1() {
        COUNTER_INCREMENT(m_metrics, tree_node_count, 1);
        COUNTER_INCREMENT(m_metrics, tree_op_write_count, 4);
        COUNTER_INCREMENT(m_metrics, tree_op_write_count, 8);
        COUNTER_INCREMENT(m_metrics, tree_op_read_count, 24);
        COUNTER_INCREMENT(m_metrics, tree_obj_count, 48);
    }

    void update2() {
        COUNTER_INCREMENT(m_metrics, tree_node_count, 5);
        COUNTER_INCREMENT(m_metrics, tree_op_write_count, 20);
        COUNTER_INCREMENT(m_metrics, tree_op_read_count, 30);
        COUNTER_INCREMENT(m_metrics, tree_op_read_count, 50);
        COUNTER_INCREMENT(m_metrics, tree_obj_count, 100);
    }
};

class Cache {
private:
    CacheMetrics m_metrics;

public:
    Cache(const char* inst_name) : m_metrics(inst_name) {}
    void update1() {
        GAUGE_UPDATE(m_metrics, cache_size, 1);
        GAUGE_UPDATE(m_metrics, cache_size, 4);
        GAUGE_UPDATE(m_metrics, cache_eviction_pct, 8);
        GAUGE_UPDATE(m_metrics, cache_writes_rate, 2);

#ifndef NDEBUG
        ASSERT_DEATH(GAUGE_UPDATE(m_metrics, invalid_gauge, 2),
                     "Metric name 'invalid_gauge' not registered yet but used");
#endif

        HISTOGRAM_OBSERVE(m_metrics, cache_write_latency, 100);
        HISTOGRAM_OBSERVE(m_metrics, cache_write_latency, 150);
        HISTOGRAM_OBSERVE(m_metrics, cache_read_latency, 150);
        HISTOGRAM_OBSERVE(m_metrics, cache_delete_latency, 200);
    }

    void update2() {
        GAUGE_UPDATE(m_metrics, cache_size, 20);
        GAUGE_UPDATE(m_metrics, cache_eviction_pct, 30);
        GAUGE_UPDATE(m_metrics, cache_writes_rate, 200);

#ifndef NDEBUG
        ASSERT_DEATH(GAUGE_UPDATE(m_metrics, invalid_gauge, 2),
                     "Metric name 'invalid_gauge' not registered yet but used");
#endif

        HISTOGRAM_OBSERVE(m_metrics, cache_write_latency, 200);
        HISTOGRAM_OBSERVE(m_metrics, cache_delete_latency, 150);
        HISTOGRAM_OBSERVE(m_metrics, cache_read_latency, 350);
        HISTOGRAM_OBSERVE(m_metrics, cache_delete_latency, 400);
    }

private:
    void write_invald_gauge() { GAUGE_UPDATE(m_metrics, invalid_gauge, 2); }
};

class MyServer {
public:
    MyServer() = default;

    void process() {
        COUNTER_INCREMENT(m_metrics, num_open_connections, 3);
        GAUGE_UPDATE(m_metrics, mem_utilization, 540);
        HISTOGRAM_OBSERVE(m_metrics, request_per_txn, 8)

        COUNTER_DECREMENT(m_metrics, num_open_connections, 2);
        GAUGE_UPDATE(m_metrics, mem_utilization, 980);

        HISTOGRAM_OBSERVE(m_metrics, request_per_txn, 16);
        HISTOGRAM_OBSERVE(m_metrics, request_per_txn, 48);
        HISTOGRAM_OBSERVE(m_metrics, request_per_txn, 1);
    }

private:
    GlobalMetrics m_metrics;
};

// clang-format off
nlohmann::json expected = {
    {"Cache", {
        {"cache1", {
            {"Counters", {}},
            {"Gauges", {
                {"Cache Eviction Percent", 8},
                {"Cache Write rate", 2},
                {"Total cache size", 4}
            }},
            {"Histograms percentiles (usecs) avg/50/95/99", {
                {"Cache Delete Latency", "200.0 / 0.0 / 0.0 / 0.0" },
                {"Cache Read Latency", "150.0 / 0.0 / 0.0 / 0.0" },
                {"Cache Write Latency", "125.0 / 99.0 / 99.0 / 99.0"}
            }}
        }},
        {"cache1_2", {
            {"Counters", {}},
            {"Gauges", {
                {"Cache Eviction Percent", 30},
                {"Cache Write rate", 200},
                {"Total cache size", 20}
            }},
            {"Histograms percentiles (usecs) avg/50/95/99", {
                {"Cache Delete Latency", "275.0 / 152.0 / 152.0 / 152.0" },
                {"Cache Read Latency", "350.0 / 0.0 / 0.0 / 0.0" },
                {"Cache Write Latency", "200.0 / 0.0 / 0.0 / 0.0"}
            }}
        }},
    }},
    {"Tree", {
        {"tree1", {
            {"Counters", {
                {"Total number of nodes in tree", 1},
                {"Total number of read ops in tree", 24},
                {"Total number of write ops in tree", 12},
                {"Total tree object count", 48},
            }},
            {"Gauges", {}},
            {"Histograms percentiles (usecs) avg/50/95/99", {}}
        }},
        {"tree2", {
            {"Counters", {
                {"Total number of nodes in tree", 5 },
                {"Total number of read ops in tree", 80 },
                {"Total number of write ops in tree", 20 },
                {"Total tree object count", 100 }
            }},
            {"Gauges", {}},
            {"Histograms percentiles (usecs) avg/50/95/99", {}}
        }}
    }},
    {"Global", {
        {"Instance1", {
            {"Counters", {
                {"Total number of connections", 1}
            }},
            {"Gauges", {
                {"Total memory utilization", 980}
            }},
            {"Histograms percentiles (usecs) avg/50/95/99", {
                {"Distribution of request per transactions", "18.25 / 15.0 / 31.0 / 31.0"}
            }}
        }}
    }}
};
// clang-format on

TEST(counterTest, wrapperTest) {
    Tree tree1("tree1"), tree2("tree2");
    tree1.update1();
    tree2.update2();

    Cache cache1("cache1"), cache2("cache1");
    cache1.update1();
    cache2.update2();

    MyServer server;
    server.process();

    auto output = MetricsFarm::getInstance().get_result_in_json();
    nlohmann::json patch = nlohmann::json::diff(output, expected);
    EXPECT_EQ(patch.empty(), true);
    if (!patch.empty()) {
        std::cerr << "Actual     " << std::setw(4) << output << "\n";
        std::cerr << "Expected   " << std::setw(4) << expected << "\n";
        std::cerr << "Diff patch " << std::setw(4) << patch << "\n";
    }

    auto prometheus_bytes = MetricsFarm::getInstance().report(ReportFormat::kTextFormat);
    // std::cout << "Prometheus serialized format: " << prometheus_bytes << "\n";
}

// SDS_OPTIONS_ENABLE(logging)
int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    // sisl_logging::SetLogger("metrics_wrapper_test");
    // spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");
    return RUN_ALL_TESTS();
}
