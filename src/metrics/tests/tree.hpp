/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#include "metrics.hpp"

#ifndef ASYNC_HTTP_TREE_HPP
#define ASYNC_HTTP_TREE_HPP

using namespace sisl;

class TreeMetrics : public MetricsGroupWrapper {
public:
    explicit TreeMetrics(const char* grp_name) : MetricsGroupWrapper(grp_name) {
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
    Tree(const char* grp_name);
    void update();
};

#endif // ASYNC_HTTP_TREE_HPP
