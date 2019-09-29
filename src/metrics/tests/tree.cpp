//
// Created by Kadayam, Hari on 1/10/19.
//

#include "tree.hpp"


Tree::Tree(const char *grp_name) : m_metrics(grp_name) {}
void Tree::update() {
    COUNTER_INCREMENT(m_metrics, tree_node_count, 1);
    COUNTER_INCREMENT(m_metrics, tree_obj_count, 4);
    COUNTER_INCREMENT(m_metrics, tree_obj_count, 8);
    COUNTER_INCREMENT(m_metrics, tree_txns, 2);
}
