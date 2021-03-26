#include <iostream>
#include <sds_logging/logging.h>
#include <sds_options/options.h>
#include <gtest/gtest.h>
#include <string>
#include <random>

#include "range_hashmap.hpp"

using namespace sisl;
SDS_LOGGING_INIT(test_hashnode)

struct TestRangeKey {
    uint64_t num{0};
    uint16_t count{0};

    std::string to_string() const { return fmt::format("[{}-{}]", num, num + count); }
};

struct TestRangeValue {
public:
    TestRangeValue(const uint64_t& d1, const uint64_t& offset = 0) : m_base{d1}, m_offset{offset} {}
    TestRangeValue(const TestRangeValue& v) = default;

    bool operator==(const TestRangeValue& v) const { return (m_base == v.m_base); }
    static std::string to_string(const TestRangeValue& v) { return fmt::format("{}", v.m_base + v.m_offset); }

    static void extract(TestRangeValue* v, const koffset_range_t& extract_range, uint8_t* new_buf) {
        if (new_buf != nullptr) {
            new (new_buf) TestRangeValue(v->m_base, v->m_offset + extract_range.second);
        } else {
            v->m_offset += extract_range.first;
        }
    }

private:
    uint64_t m_base;
    uint64_t m_offset;
};

DECLARE_RELOCATABLE(TestRangeValue)

static thread_local std::random_device g_rd{};
static thread_local std::default_random_engine g_re{g_rd()};
static std::uniform_int_distribution< uint64_t > g_rand_generator{1000, 10000};
static std::uniform_int_distribution< koffset_t > g_offset_generator{0, 255};
static std::uniform_int_distribution< koffset_t > g_size_generator{1, 32};

struct HashNodeTest : public testing::Test {
public:
    virtual ~HashNodeTest() override = default;

protected:
    void SetUp() override { m_node = MultiEntryHashNode< TestRangeKey, TestRangeValue >::alloc_node(); }
    void TearDown() override { delete m_node; }

    void insert_range(const uint8_t start, const uint8_t end, const bool expected_success) {
        bool is_resized;
        uint64_t d = g_rand_generator(g_re);
        auto val = TestRangeValue{d, 0};

        std::tie(m_node, is_resized) = MultiEntryHashNode< TestRangeKey, TestRangeValue >::resize_if_needed(m_node, 1);
        auto [v, success] = m_node->try_emplace(std::make_pair<>(start, end), val);

        ASSERT_EQ(expected_success, success) << "emplace returned unexpected status";
        if (success) {
            auto i{start};
            while (true) {
                m_shadow_map.insert({i, val});
                if (i == end) break;
                ++i;
            }
        }
    }

    void validate_range(const uint8_t start, const uint8_t end) const {
        std::vector< const MultiEntryHashNode< TestRangeKey, TestRangeValue >::val_entry_info* > entries;
        int n = m_node->find(koffset_range_t{start, end}, entries);
        ASSERT_EQ(entries.size(), s_cast< size_t >(n)) << "find return does not match vector entries";

        for (const auto& e : entries) {
            for (auto o{e->range.first}; o < e->range.second; ++o) {
                auto it = m_shadow_map.find(o);
                ASSERT_EQ(*(e->get_value_const()), it->second) << "Value mismatch for offset=" << (int)o;
            }
        }
    }

    void erase_range(const uint8_t start, const uint8_t end) {
        bool is_resized;
        std::tie(m_node, is_resized) = MultiEntryHashNode< TestRangeKey, TestRangeValue >::resize_if_needed(m_node, 1);
        auto erased_count = m_node->erase(std::make_pair<>(start, end), TestRangeValue::extract);
        ASSERT_EQ(erased_count, end - start + 1) << "erase return of count does not match expected";

        auto i{start};
        while (true) {
            m_shadow_map.erase(i);
            if (i == end) break;
            ++i;
        }
    }

    void validate_all(const uint8_t in_count_of = 8) {
        LOGINFO("INFO: Read it back (and validate) in range of {}", in_count_of);
        for (uint16_t k{0}; k <= 256 - in_count_of; k += in_count_of) {
            validate_range(k, k + in_count_of);
        }
    }

protected:
    MultiEntryHashNode< TestRangeKey, TestRangeValue >* m_node;
    std::map< uint8_t, TestRangeValue > m_shadow_map;
};

TEST_F(HashNodeTest, SequentialTest) {
    LOGINFO("INFO: Insert all items in the range of 4");
    for (uint16_t k{0}; k <= 252; k += 4) {
        insert_range(k, k + 3, true);
        insert_range(k, k + 1, false);
    }
    validate_all();

    LOGINFO("INFO: Erase the middle of the range");
    for (uint16_t k{0}; k <= 252; k += 4) {
        erase_range(k + 1, k + 2);
    }
    validate_all();

    LOGINFO("INFO: Erase the last in the range of 4");
    for (uint16_t k{0}; k <= 252; k += 4) {
        erase_range(k + 3, k + 3);
    }
    validate_all();

    LOGINFO("INFO: ReInsert 2nd in the range");
    for (uint16_t k{0}; k <= 252; k += 4) {
        insert_range(k + 1, k + 1, true);
    }
    validate_all();

    LOGINFO("INFO: ReInsert 3rd in the range");
    for (uint16_t k{0}; k <= 252; k += 4) {
        insert_range(k + 2, k + 2, true);
    }
    validate_all();

    LOGINFO("Node details: {}", m_node->to_string());
}

TEST_F(HashNodeTest, RandomTest) {
    LOGINFO("INFO: Insert all items in the range of 4");
    uint32_t offset{0};
    const uint32_t max_offset{(1u << (sizeof(koffset_t) * 8)) - 1};
    while (offset < max_offset) {
        const auto sz{g_size_generator(g_re)};
        const koffset_t s{s_cast< koffset_t >(offset)};
        LOGINFO("Inserting range {} to {} cur_offset={}", s, s + std::min(sz, s_cast< koffset_t >(max_offset - offset)),
                offset);
        insert_range(s, s + std::min(sz, s_cast< koffset_t >(max_offset - offset)), true);
        offset += sz + 1;
    }
    validate_all();

    LOGINFO("Node details: {}", m_node->to_string());
}

SDS_OPTIONS_ENABLE(logging, test_hashnode)
SDS_OPTION_GROUP(test_hashnode,
                 (num_threads, "", "num_threads", "number of threads",
                  ::cxxopts::value< uint32_t >()->default_value("8"), "number"),
                 (num_iters, "", "num_iters", "number of iterations",
                  ::cxxopts::value< uint64_t >()->default_value("10000"), "number"))

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SDS_OPTIONS_LOAD(argc, argv, logging, test_hashnode)
    sds_logging::SetLogger("test_hashnode");
    spdlog::set_pattern("[%D %T%z] [%^%L%$] [%t] %v");

    auto ret = RUN_ALL_TESTS();
    return ret;
}
