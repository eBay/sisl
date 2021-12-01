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
 * under the License is distributed on  * an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#include "tree.hpp"

Tree::Tree(const char* grp_name) : m_metrics(grp_name) {}
void Tree::update() {
    COUNTER_INCREMENT(m_metrics, tree_node_count, 1);
    COUNTER_INCREMENT(m_metrics, tree_obj_count, 4);
    COUNTER_INCREMENT(m_metrics, tree_obj_count, 8);
    COUNTER_INCREMENT(m_metrics, tree_txns, 2);
}
