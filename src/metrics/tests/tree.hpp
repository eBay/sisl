//
// Created by Kadayam, Hari on 1/10/19.
//
#include "metrics.hpp"

#ifndef ASYNC_HTTP_TREE_HPP
#define ASYNC_HTTP_TREE_HPP

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

class Tree {
private:
    TreeMetrics m_metrics;

public:
    Tree(const char *grp_name);
    void update();
};

#endif //ASYNC_HTTP_TREE_HPP
