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

#include <gtest/gtest.h>
#include <string>
#include <random>
#include <map>

#include "options/options.h"
#include "logging/logging.h"
#include "simple_node.hpp"
#include "utility/enum.hpp"
#include "fds/bitset.hpp"

static constexpr uint32_t g_node_size{4096};
static constexpr uint32_t g_max_keys{6000};
static thread_local std::random_device g_rd{};
static thread_local std::default_random_engine g_re{g_rd()};

using namespace sisl::btree;

class TestSimpleKey : public BtreeKey {
private:
    uint32_t m_key{0};

public:
    TestSimpleKey() = default;
    TestSimpleKey(uint32_t k) : m_key{k} {}
    TestSimpleKey(const TestSimpleKey& other) = default;
    TestSimpleKey(const sisl::blob& b, bool copy) { m_key = *(r_cast< const uint32_t* >(b.bytes)); }
    TestSimpleKey& operator=(const TestSimpleKey& other) = default;
    virtual ~TestSimpleKey() = default;

    int compare(const BtreeKey& o) const override {
        const TestSimpleKey& other = s_cast< const TestSimpleKey& >(o);
        if (m_key < other.m_key) {
            return -1;
        } else if (m_key > other.m_key) {
            return 1;
        } else {
            return 0;
        }
    }

    int compare_range(const BtreeKeyRange& range) const override {
        if (m_key == start_key(range)) {
            return range.is_start_inclusive() ? 0 : -1;
        } else if (m_key < start_key(range)) {
            return -1;
        } else if (m_key == end_key(range)) {
            return range.is_end_inclusive() ? 0 : 1;
        } else if (m_key > end_key(range)) {
            return 1;
        } else {
            return 0;
        }
    }

    sisl::blob serialize() const override {
        return sisl::blob{uintptr_cast(const_cast< uint32_t* >(&m_key)), uint32_cast(sizeof(uint32_t))};
    }
    uint32_t serialized_size() const override { return get_fixed_size(); }
    static uint32_t get_fixed_size() { return (sizeof(uint32_t)); }
    std::string to_string() const { return fmt::format("{}", m_key); }

    friend std::ostream& operator<<(std::ostream& os, const TestSimpleKey& k) {
        os << k.to_string();
        return os;
    }

    bool operator<(const TestSimpleKey& o) const { return (compare(o) < 0); }
    bool operator==(const TestSimpleKey& other) const { return (compare(other) == 0); }

    uint32_t key() const { return m_key; }
    uint32_t start_key(const BtreeKeyRange& range) const {
        const TestSimpleKey& k = (const TestSimpleKey&)(range.start_key());
        return k.m_key;
    }
    uint32_t end_key(const BtreeKeyRange& range) const {
        const TestSimpleKey& k = (const TestSimpleKey&)(range.end_key());
        return k.m_key;
    }
};

class TestSimpleValue : public BtreeValue {
public:
    TestSimpleValue(bnodeid_t val) { assert(0); }
    TestSimpleValue(uint32_t val) : BtreeValue() { m_val = val; }
    TestSimpleValue() : TestSimpleValue((uint32_t)-1) {}
    TestSimpleValue(const TestSimpleValue& other) : BtreeValue() { m_val = other.m_val; };
    TestSimpleValue(const sisl::blob& b, bool copy) : BtreeValue() { m_val = *(r_cast< uint32_t* >(b.bytes)); }
    virtual ~TestSimpleValue() = default;

    TestSimpleValue& operator=(const TestSimpleValue& other) {
        m_val = other.m_val;
        return *this;
    }

    sisl::blob serialize() const override {
        sisl::blob b;
        b.bytes = uintptr_cast(const_cast< uint32_t* >(&m_val));
        b.size = sizeof(m_val);
        return b;
    }

    uint32_t serialized_size() const override { return sizeof(m_val); }
    static uint32_t get_fixed_size() { return sizeof(m_val); }

    std::string to_string() const override { return fmt::format("{}", m_val); }

    friend ostream& operator<<(ostream& os, const TestSimpleValue& v) {
        os << v.to_string();
        return os;
    }

    // This is not mandatory overridden method for BtreeValue, but for testing comparision
    bool operator==(const TestSimpleValue& other) const { return (m_val == other.m_val); }

    uint32_t value() const { return m_val; }

private:
    uint32_t m_val;
};

static std::uniform_int_distribution< uint32_t > g_randval_generator{1, 30000};

struct SimpleNodeTest : public testing::Test {
protected:
    std::unique_ptr< SimpleNode< TestSimpleKey, TestSimpleValue > > m_node1;
    std::unique_ptr< SimpleNode< TestSimpleKey, TestSimpleValue > > m_node2;
    std::map< uint32_t, uint32_t > m_shadow_map;

protected:
    void SetUp() override {
        m_node1 =
            std::make_unique< SimpleNode< TestSimpleKey, TestSimpleValue > >(new uint8_t[g_node_size], 1ul, true, true);
        m_node2 =
            std::make_unique< SimpleNode< TestSimpleKey, TestSimpleValue > >(new uint8_t[g_node_size], 2ul, true, true);
    }

    void put(uint32_t k, btree_put_type put_type) {
        uint32_t v = g_randval_generator(g_re);
        auto* node = (k < g_max_keys / 2) ? m_node1.get() : m_node2.get();
        TestSimpleValue existing_v;
        bool done = node->put(TestSimpleKey{k}, TestSimpleValue{v}, put_type, &existing_v);

        bool expected_done{true};
        if (m_shadow_map.find(k) != m_shadow_map.end()) {
            expected_done = (put_type == btree_put_type::INSERT_ONLY_IF_NOT_EXISTS);
        }
        ASSERT_EQ(done, expected_done) << "Expected put of key " << k << " of put_type " << enum_name(put_type)
                                       << " to be " << expected_done;
        if (expected_done) {
            m_shadow_map.insert({k, v});
        } else {
            const auto r = m_shadow_map.find(k);
            ASSERT_NE(r, m_shadow_map.end()) << "Testcase issue, expected inserted slots to be in shadow map";
            ASSERT_EQ(existing_v.value(), r->second)
                << "Insert existing value doesn't return correct data for key " << r->first;
        }
    }

    void put_list(const std::vector< uint32_t >& keys) {
        for (const auto& k : keys) {
            put(k, btree_put_type::INSERT_ONLY_IF_NOT_EXISTS);
        }
    }

    void print() const {
        LOGDEBUG("Node1:\n {}", m_node1->to_string(true));
        LOGDEBUG("Node2:\n {}", m_node2->to_string(true));
    }

    void update(uint32_t k, bool validate_update = true) {
        uint32_t v = g_randval_generator(g_re);
        auto* node = (k < g_max_keys / 2) ? m_node1.get() : m_node2.get();
        TestSimpleValue existing_v;
        const bool done = node->update_one(TestSimpleKey{k}, TestSimpleValue{v}, nullptr, &existing_v);
        const auto expected_done = (m_shadow_map.find(k) != m_shadow_map.end());
        ASSERT_EQ(done, expected_done) << "Not updated for key=" << k << " where it is expected to";

        if (done) {
            validate_data(k, existing_v);
            m_shadow_map[k] = v;
        }

        if (validate_update) { validate_specific(k); }
    }

    void remove(uint32_t k, bool validate_remove = true) {
        TestSimpleKey key;
        TestSimpleValue value;
        const bool shadow_found = (m_shadow_map.find(k) != m_shadow_map.end());
        auto removed_1 = m_node1->remove_one(BtreeKeyRange{TestSimpleKey{k}}, &key, &value);
        if (removed_1) {
            ASSERT_EQ(key.key(), k) << "Whats removed is different than whats asked for";
            validate_data(k, value);
            m_shadow_map.erase(k);
        }

        auto removed_2 = m_node2->remove_one(BtreeKeyRange{TestSimpleKey{k}}, &key, &value);
        if (removed_2) {
            ASSERT_EQ(key.key(), k) << "Whats removed is different than whats asked for";
            validate_data(k, value);
            m_shadow_map.erase(k);
        }

        ASSERT_EQ(removed_1 || removed_2, shadow_found) << "To remove key=" << k << " is not present in the nodes";

        if (validate_remove) { validate_specific(k); }
    }

    void validate_get_all() const {
        uint32_t start_ind{0};
        uint32_t end_ind{0};
        std::vector< std::pair< TestSimpleKey, TestSimpleValue > > out_vector;
        auto ret = m_node1->get_all(BtreeKeyRange{TestSimpleKey{0u}, true, TestSimpleKey{g_max_keys}, false},
                                    g_max_keys, start_ind, end_ind, &out_vector);
        ret += m_node2->get_all(BtreeKeyRange{TestSimpleKey{0u}, true, TestSimpleKey{g_max_keys}, false}, g_max_keys,
                                start_ind, end_ind, &out_vector);

        ASSERT_EQ(ret, m_shadow_map.size()) << "Expected number of entries to be same with shadow_map size";
        ASSERT_EQ(out_vector.size(), m_shadow_map.size())
            << "Expected number of entries to be same with shadow_map size";

        uint64_t idx{0};
        for (auto& [k, v] : m_shadow_map) {
            ASSERT_EQ(out_vector[idx].second.value(), v)
                << "Range get doesn't return correct data for key=" << k << " idx=" << idx;
            ++idx;
        }
    }

    void validate_get_any(uint32_t start, uint32_t end) const {
        TestSimpleKey out_k;
        TestSimpleValue out_v;
        auto result = m_node1->find(BtreeKeyRange{TestSimpleKey{start}, true, TestSimpleKey{end}, true}, &out_k, &out_v,
                                    true, true);
        if (result.first) {
            validate_data(out_k.key(), out_v);
        } else {
            result = m_node2->find(BtreeKeyRange{TestSimpleKey{start}, true, TestSimpleKey{end}, true}, &out_k, &out_v,
                                   true, true);
            if (result.first) {
                validate_data(out_k.key(), out_v);
            } else {
                const auto r = m_shadow_map.lower_bound(start);
                const bool found = ((r == m_shadow_map.end()) || (r->first <= end));
                ASSERT_EQ(found, false) << "Node key range=" << start << "-" << end
                                        << " missing, Its present in shadow map at " << r->first;
            }
        }
    }

    void validate_specific(uint32_t k) {
        TestSimpleValue val;
        auto* node = (k < g_max_keys / 2) ? m_node1.get() : m_node2.get();
        const auto ret = node->find(TestSimpleKey{k}, &val, true);
        ASSERT_EQ(ret.first, m_shadow_map.find(k) != m_shadow_map.end())
            << "Node key " << k << " is incorrect presence compared to shadow map";
        if (ret.first) { validate_data(k, val); }
    }

private:
    void validate_data(uint32_t key, const TestSimpleValue& node_val) const {
        const auto r = m_shadow_map.find(key);
        ASSERT_NE(r, m_shadow_map.end()) << "Node key is not present in shadow map";
        ASSERT_EQ(node_val.value(), r->second)
            << "Found value in node doesn't return correct data for key=" << r->first;
    }
};

TEST_F(SimpleNodeTest, SequentialInsert) {
    put_list({0, 1, 2, g_max_keys / 2, g_max_keys / 2 + 1, g_max_keys / 2 - 1});
    print();
    validate_get_all();
    validate_get_any(0, 2);
    validate_get_any(3, 3);
    validate_get_any(g_max_keys / 2, g_max_keys - 1);
}

TEST_F(SimpleNodeTest, Remove) {
    put_list({0, 1, 2, g_max_keys / 2, g_max_keys / 2 + 1, g_max_keys / 2 - 1});
    remove(0);
    remove(0); // Remove non-existing
    remove(1);
    remove(2);
    remove(g_max_keys / 2 - 1);
    print();
    validate_get_all();
    validate_get_any(0, 2);
    validate_get_any(3, 3);
    validate_get_any(g_max_keys / 2, g_max_keys - 1);
}

TEST_F(SimpleNodeTest, Update) {
    put_list({0, 1, 2, g_max_keys / 2, g_max_keys / 2 + 1, g_max_keys / 2 - 1});
    update(1);
    update(g_max_keys / 2);
    update(2);
    remove(0);
    update(0); // Update non-existing
    print();
    validate_get_all();
}

TEST_F(SimpleNodeTest, Move) {
    std::vector< uint32_t > list{0, 1, 2, g_max_keys / 2 - 1};
    put_list(list);
    print();
    BtreeConfig cfg{1024};
    cfg.set_node_area_size(1024);

    m_node1->move_out_to_right_by_entries(cfg, *m_node2, list.size());
    m_node1->move_out_to_right_by_entries(cfg, *m_node2, list.size()); // Empty move
    ASSERT_EQ(m_node1->get_total_entries(), 0u) << "Move out to right has failed";
    ASSERT_EQ(m_node2->get_total_entries(), list.size()) << "Move out to right has failed";
    validate_get_all();

    m_node1->move_in_from_right_by_entries(cfg, *m_node2, list.size());
    m_node1->move_in_from_right_by_entries(cfg, *m_node2, list.size()); // Empty move
    ASSERT_EQ(m_node2->get_total_entries(), 0u) << "Move in from right has failed";
    ASSERT_EQ(m_node1->get_total_entries(), list.size()) << "Move in from right has failed";
    validate_get_all();

    m_node1->move_out_to_right_by_entries(cfg, *m_node2, list.size() / 2);
    ASSERT_EQ(m_node1->get_total_entries(), list.size() / 2) << "Move out half entries to right has failed";
    ASSERT_EQ(m_node2->get_total_entries(), list.size() - list.size() / 2)
        << "Move out half entries to right has failed";
    validate_get_all();
    print();

    ASSERT_EQ(m_node1->validate_key_order(), true) << "Key order validation of node1 has failed";
    ASSERT_EQ(m_node2->validate_key_order(), true) << "Key order validation of node2 has failed";
}

SISL_OPTIONS_ENABLE(logging, test_btree_node)
SISL_OPTION_GROUP(test_btree_node,
                  (num_iters, "", "num_iters", "number of iterations for rand ops",
                   ::cxxopts::value< uint32_t >()->default_value("65536"), "number"))

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging, test_btree_node)
    sisl::logging::SetLogger("test_btree_node");
    spdlog::set_pattern("[%D %T%z] [%^%L%$] [%t] %v");

    auto ret = RUN_ALL_TESTS();
    return ret;
}