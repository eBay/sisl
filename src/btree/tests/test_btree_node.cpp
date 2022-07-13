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
    TestSimpleValue(bnodeid_t val) {}
    TestSimpleValue(uint32_t val) : BtreeValue() { m_val = val; }
    TestSimpleValue() : TestSimpleValue((uint32_t)-1) {}
    TestSimpleValue(const TestSimpleValue& other) { m_val = other.m_val; }
    TestSimpleValue(const sisl::blob& b, bool copy) { m_val = *(r_cast< uint32_t* >(b.bytes)); }

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

struct SimpleNodeTest : public testing::Test {
protected:
    std::unique_ptr< SimpleNode< TestSimpleKey, TestSimpleValue > > m_node1;
    std::unique_ptr< SimpleNode< TestSimpleKey, TestSimpleValue > > m_node2;
    std::map< uint32_t, uint32_t > m_shadow_map;
    sisl::Bitset m_inserted_slots{g_max_keys};

protected:
    void SetUp() override {
        m_node1 =
            std::make_unique< SimpleNode< TestSimpleKey, TestSimpleValue > >(new uint8_t[g_node_size], 1ul, true, true);
        m_node2 =
            std::make_unique< SimpleNode< TestSimpleKey, TestSimpleValue > >(new uint8_t[g_node_size], 2ul, true, true);
    }

    void put(uint32_t k, btree_put_type put_type) {
        static std::uniform_int_distribution< uint32_t > g_randval_generator{1, 30000};
        uint32_t v = g_randval_generator(g_re);
        auto* node = (k < g_max_keys / 2) ? m_node1.get() : m_node2.get();
        TestSimpleValue existing_v;
        bool done = node->put(TestSimpleKey{k}, TestSimpleValue{v}, put_type, &existing_v);

        bool expected_done{true};
        if (m_inserted_slots.is_bits_set(k, 1)) {
            expected_done = (put_type == btree_put_type::INSERT_ONLY_IF_NOT_EXISTS);
        }
        ASSERT_EQ(done, expected_done) << "Expected put of key " << k << " of put_type " << enum_name(put_type)
                                       << " to be " << expected_done;
        if (expected_done) {
            m_shadow_map.insert({k, v});
            m_inserted_slots.set_bit(k);
        } else {
            const auto r = m_shadow_map.find(k);
            ASSERT_NE(r, m_shadow_map.end()) << "Testcase issue, expected inserted slots to be in shadow map";
            ASSERT_EQ(existing_v.value(), r->second)
                << "Insert existing value doesn't return correct data for key " << r->first;
        }
    }

    void print() const {
        std::cout << "Node1:\n" << m_node1->to_string(true) << "\n";
        std::cout << "Node2:\n" << m_node2->to_string(true) << "\n";
    }

    void remove(uint32_t k) {}

    void validate_get_all() const {
        uint32_t start_ind{0};
        uint32_t end_ind{0};
        std::vector< std::pair< TestSimpleKey, TestSimpleValue > > out_vector;
        auto ret = m_node1->get_all(BtreeKeyRange{TestSimpleKey{0u}, true, TestSimpleKey{g_max_keys / 2}, false},
                                    g_max_keys / 2, start_ind, end_ind, &out_vector);
        ASSERT_EQ(start_ind, 0u) << "Expected the entire range with start = 0";
        ASSERT_EQ(end_ind, m_node1->get_total_entries() - 1) << "Expected the end index to be the last entry";

        ret += m_node2->get_all(BtreeKeyRange{TestSimpleKey{g_max_keys / 2}, true, TestSimpleKey{g_max_keys}, false},
                                g_max_keys / 2, start_ind, end_ind, &out_vector);
        ASSERT_EQ(start_ind, 0u) << "Expected the entire range with start = 0";
        ASSERT_EQ(end_ind, m_node2->get_total_entries() - 1) << "Expected the end index to be the last entry";
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

    void validate_data(uint32_t key, const TestSimpleValue& node_val) const {
        const auto r = m_shadow_map.find(key);
        ASSERT_NE(r, m_shadow_map.end()) << "Node key is not present in shadow map";
        ASSERT_EQ(node_val.value(), r->second)
            << "Found value in node doesn't return correct data for key=" << r->first;
    }
#if 0
    void validate_range(const uint32_t start, const uint32_t end) const {
        auto entries = m_map->get(RangeKey{1u, start, end - start + 1});

        for (const auto& [key, val] : entries) {
            ASSERT_EQ(key.m_base_key, 1u) << "Expected base key is standard value 1";
            uint8_t* got_bytes = val.bytes();
            for (auto o{key.m_nth}; o < key.m_nth + key.m_count; ++o) {
                auto it = m_shadow_map.find(o);
                ASSERT_EQ(m_inserted_slots.is_bits_set(o, 1), true) << "Found a key " << o << " which was not inserted";
                compare_data(o, got_bytes, it->second.bytes);
                got_bytes += per_val_size;
            }
        }
    }

    void validate_all() { validate_range(0, g_max_offset - 1); }

    void erase_range(const uint32_t start, const uint32_t end) {
        m_map->erase(RangeKey{1u, start, end - start + 1});

        for (auto i{start}; i <= end; ++i) {
            m_shadow_map.erase(i);
            m_inserted_slots.reset_bit(i);
        }
    }

    sisl::io_blob create_data(const uint32_t start, const uint32_t end) {
        auto blob = sisl::io_blob{per_val_size * (end - start + 1), 0};
        uint8_t* bytes = blob.bytes;

        for (auto i = start; i <= end; ++i) {
            auto arr = (std::array< uint32_t, per_val_size / sizeof(uint32_t) >*)bytes;
            std::fill(arr->begin(), arr->end(), i);
            bytes += per_val_size;
        }
        return blob;
    }

    void compare_data(const uint32_t offset, const uint8_t* l_bytes, const uint8_t* r_bytes) const {
        const auto l_arr = (std::array< uint32_t, per_val_size / sizeof(uint32_t) >*)l_bytes;
        const auto r_arr = (std::array< uint32_t, per_val_size / sizeof(uint32_t) >*)r_bytes;

        for (size_t i{0}; i < l_arr->size(); ++i) {
            ASSERT_EQ(l_arr->at(i), r_arr->at(i)) << "Mismatch of bytes at byte=" << i << " on offset=" << offset;
            ASSERT_EQ(l_arr->at(i), offset) << "Expected data to be same as offset=" << offset;
        }
    }
#endif
};

TEST_F(SimpleNodeTest, SequentialInsert) {
    put(0, btree_put_type::INSERT_ONLY_IF_NOT_EXISTS);
    put(1, btree_put_type::INSERT_ONLY_IF_NOT_EXISTS);
    put(2, btree_put_type::INSERT_ONLY_IF_NOT_EXISTS);
    put(g_max_keys / 2, btree_put_type::INSERT_ONLY_IF_NOT_EXISTS);
    put(g_max_keys / 2 + 1, btree_put_type::INSERT_ONLY_IF_NOT_EXISTS);
    put(g_max_keys / 2 - 1, btree_put_type::INSERT_ONLY_IF_NOT_EXISTS);
    print();
    validate_get_all();
    validate_get_any(0, 2);
    validate_get_any(3, 3);
    validate_get_any(g_max_keys / 2, g_max_keys - 1);
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