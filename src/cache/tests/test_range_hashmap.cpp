#include <iostream>
#include <sds_options/options.h>
#include <gtest/gtest.h>
#include <string>
#include <random>

#include "logging/logging.h"
#include "fds/bitset.hpp"
#include "range_hashmap.hpp"
#include "utility/enum.hpp"

using namespace sisl;
SISL_LOGGING_INIT(test_hashmap)

#if 0
struct TestRangeKey {
    uint64_t m_num{0};
    uint32_t m_count{0};

    TestRangeKey() = default;
    TestRangeKey(const uint64_t n, const uint32_t c) : m_num{n}, m_count{c} {}

    std::pair< koffset_t, uint32_t > get_subset_from(const TestRangeKey& base) {
        return std::make_pair<>(m_num - base.m_num, base.m_num + base.m_count - m_num);
    }

    TestRangeKey get_base_key(const uint32_t split_boundary) {
        return TestRangeKey{sisl::round_down(key.m_num, split_boundary), split_boundary};
    }

    // Take the range key and split them by modulo and then returning n different keyviews of it
    static void split(const TestRangeKey& key, const uint32_t split_boundary, std::vector< KeyView >& out_views) {
        auto base_num = sisl::round_down(key.m_num, split_boundary);

        auto base_key = key.get_base_key(split_boundary);
        auto [offset, sub_size] = key.get_subset_from(base_key);
        if (sub_size > key.m_count) { sub_size = key.m_count; }
        out_views.emplace_back(base_key, offset, sub_size);

        auto remain_count = key.m_count - sub_size;
        while (remain_count > 0) {
            base_key.m_num += split_boundary;
            size = std::min(split_boundary, remain_count);
            out_views.emplace_back(base_key, 0, size);
            remain_count -= size;
        }
    }

    static sisl::blob get_blob(const TestRangeKey& k) {
        return sisl::blob{r_cast< uint8_t* >(&k), sizeof(TestRangeKey)};
    }

    static int compare(const TestRangeKey& k1, const TestRangeKey& k2) {
        if (k2.m_num < k1.m_num) {
            return -1;
        } else if (k2.m_num > k1.m_num) {
            return 1;
        } else if (k2.m_count < k1.m_count) {
            return -1;
        } else if (k2.m_count > k1.m_count) {
            return 1;
        } else {
            return 0;
        }
    }

    std::string to_string() const { return fmt::format("[{}-{}]", m_num, m_num + m_count - 1); }
};

struct TestRangeValue {
public:
    TestRangeValue(const uint64_t& d1, const uint64_t& offset = 0) : m_base{d1}, m_offset{offset}, m_refcount{1} {}
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

    static bool can_erase(const TestRangeKey& k, const TestRangeValue& v) { return (--v.m_refcount == 0); }

    static void update(const TestRangeKey& base_key, const koffset_range_t range, TestRangeValue* value) {
        ++value->m_refcount;
    }

private:
    uint64_t m_base;
    uint64_t m_offset;
    int m_refcount;
};

DECLARE_RELOCATABLE(TestRangeValue)
#endif

static uint32_t g_max_offset;
static constexpr uint32_t per_val_size = 128;

static thread_local std::random_device g_rd{};
static thread_local std::default_random_engine g_re{g_rd()};
static std::uniform_int_distribution< uint64_t > g_rand_generator{1000, 10000};
static std::uniform_int_distribution< big_count_t > g_size_generator{1, 2048};
static std::uniform_int_distribution< uint8_t > g_op_generator{0, 2};

struct RangeHashMapTest : public testing::Test {
protected:
    std::unique_ptr< RangeHashMap< uint32_t > > m_map;
    std::unordered_map< uint32_t, sisl::blob > m_shadow_map;
    sisl::Bitset m_inserted_slots{g_max_offset};

protected:
    void SetUp() override { m_map = std::make_unique< RangeHashMap< uint32_t > >(1000, extract_value, nullptr); }

    void insert_range(const uint32_t start, const uint32_t end) {
        m_map->insert(RangeKey{1u, start, end - start + 1}, create_data(start, end));

        for (auto i{start}; i <= end; ++i) {
            m_shadow_map.insert({i, create_data(i, i)});
            m_inserted_slots.set_bit(i);
        }
    }

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

    static sisl::byte_view extract_value(const sisl::byte_view& inp_bytes, uint32_t nth, uint32_t count) {
        return sisl::byte_view{inp_bytes, nth * per_val_size, count * per_val_size};
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
};

TEST_F(RangeHashMapTest, SequentialTest) {
    LOGINFO("INFO: Insert all items in the range of 4");
    for (uint32_t k{0}; k < g_max_offset; k += 4) {
        insert_range(k, k + 3);
        validate_range(k, k + 2);
    }

    LOGINFO("INFO: Erase 2 items in the middle of range");
    for (uint32_t k{0}; k < g_max_offset; k += 4) {
        erase_range(k + 1, k + 2);
        validate_range(k, k + 3);
    }

    LOGINFO("INFO: Erase the last in the range of 4");
    for (uint32_t k{0}; k < g_max_offset; k += 4) {
        erase_range(k + 3, k + 3);
        validate_range(k, k + 3);
    }

    LOGINFO("INFO: ReInsert 2nd in the range");
    for (uint32_t k{0}; k < g_max_offset; k += 4) {
        insert_range(k + 1, k + 1);
        validate_range(k, k + 3);
    }

    LOGINFO("INFO: ReInsert 3rd in the range");
    for (uint32_t k{0}; k < g_max_offset; k += 4) {
        insert_range(k + 2, k + 2);
        validate_range(k, k + 3);
    }

    validate_all();
}

VENUM(op_t, uint8_t, GET = 0, INSERT = 1, ERASE = 2)

TEST_F(RangeHashMapTest, RandomEverythingTest) {
    uint32_t nread_ops{0}, ninsert_ops{0}, nerase_ops{0};
    uint32_t nblks_read{0}, nblks_inserted{0}, nblks_erased{0};

    static std::uniform_int_distribution< big_offset_t > offset_generator{0, g_max_offset - 1};

    auto num_iters = SDS_OPTIONS["num_iters"].as< uint32_t >();
    LOGINFO("INFO: Do completely random read/insert/erase operations for {} entries for {} iters", g_max_offset,
            num_iters);
    for (uint32_t i{0}; i < num_iters; ++i) {
        const op_t op = s_cast< op_t >(g_op_generator(g_re));
        const big_offset_t offset = offset_generator(g_re);
        big_count_t size = g_size_generator(g_re);
        if (size + offset >= g_max_offset) { size = g_max_offset - offset - 1; }

        LOGINFO("INFO: Doing op={} offset_range={}-{}", enum_name(op), offset, offset + size - 1);

        switch (op) {
        case op_t::GET:
            validate_range(offset, offset + size - 1);
            nblks_read += m_inserted_slots.get_set_count(offset, offset + size - 1);
            ++nread_ops;
            break;
        case op_t::INSERT: {
            insert_range(offset, offset + size - 1);
            nblks_inserted += m_inserted_slots.get_set_count(offset, offset + size - 1);
            ++ninsert_ops;
            break;
        }
        case op_t::ERASE:
            nblks_erased += m_inserted_slots.get_set_count(offset, offset + size - 1);
            erase_range(offset, offset + size - 1);
            ++nerase_ops;
            break;
        }
    }
    validate_all();
    LOGINFO("Executed read_ops={}, blks_read={} insert_ops={} blks_inserted={} erase_ops={} blks_erased={}", nread_ops,
            nblks_read, ninsert_ops, nblks_inserted, nerase_ops, nblks_erased);
}

#if 0
struct HashNodeTest : public testing::Test {
public:
    virtual ~HashNodeTest() override = default;

protected:
    void SetUp() override { m_node = MultiEntryHashNode< TestRangeKey, TestRangeValue >::alloc_node(TestRangeKey{}); }
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
                m_inserted_slots.set_bit(i);
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

    void erase_range(const uint8_t start, const uint8_t end, const uint8_t expected_count) {
        bool is_resized;
        std::tie(m_node, is_resized) = MultiEntryHashNode< TestRangeKey, TestRangeValue >::resize_if_needed(m_node, 1);
        auto erased_count = m_node->erase(std::make_pair<>(start, end), TestRangeValue::extract);
        ASSERT_EQ(erased_count, expected_count)
            << "erase return of count does not match expected for range" << start << "-" << end;
        if (erased_count == 0) { return; }

        auto i{start};
        while (true) {
            m_shadow_map.erase(i);
            m_inserted_slots.reset_bit(i);
            if (i == end) break;
            ++i;
        }
    }

    void validate_all(const uint8_t in_count_of = 8) {
        LOGDEBUG("INFO: Read it back (and validate) in range of {}", in_count_of);
        for (uint16_t k{0}; k <= 256 - in_count_of; k += in_count_of) {
            validate_range(k, k + in_count_of);
        }
        m_node->validate_keys();
    }

    std::pair< koffset_t, koffset_t > pick_to_erase(koffset_t max_nblks) {
        assert(m_shadow_map.size() != 0);
        auto start{g_offset_generator(g_re)};
        uint64_t prev{start};
        koffset_t count{0};

        max_nblks = std::min((g_max_offset - start) + 1, s_cast< uint32_t >(max_nblks));
        do {
            auto b = m_inserted_slots.get_next_set_bit(prev);
            if (b == prev) {
                ++prev;
                ++count;
            } else if (count > 0) {
                break;
            } else if (b == sisl::Bitset::npos) {
                start = 0;
                prev = 0;
            } else {
                start = b;
                prev = b + 1;
                count = 1;
            }
        } while (count < max_nblks);

        assert(count > 0);
        return std::make_pair(s_cast< koffset_t >(start), s_cast< koffset_t >(start + count - 1));
    }

    std::pair< koffset_t, koffset_t > pick_to_insert(const koffset_t max_nblks) {
        assert(m_shadow_map.size() < g_max_offset + 1);
        auto start_offset{g_offset_generator(g_re)};
        auto bb = m_inserted_slots.get_next_contiguous_n_reset_bits(start_offset, std::nullopt, 1, max_nblks);
        if (bb.nbits == 0) { bb = m_inserted_slots.get_next_contiguous_n_reset_bits(0, std::nullopt, 1, max_nblks); }
        assert(bb.nbits > 0);
        return std::make_pair(s_cast< koffset_t >(bb.start_bit), s_cast< koffset_t >(bb.start_bit + bb.nbits - 1));
    }

protected:
    MultiEntryHashNode< TestRangeKey, TestRangeValue >* m_node;
    std::unordered_map< uint8_t, TestRangeValue > m_shadow_map;
    sisl::Bitset m_inserted_slots{g_max_offset + 1};
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
        erase_range(k + 1, k + 2, 2);
    }
    validate_all();

    LOGINFO("INFO: Erase the last in the range of 4");
    for (uint16_t k{0}; k <= 252; k += 4) {
        erase_range(k + 3, k + 3, 1);
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

    LOGINFO("Node details after test: {}", m_node->to_string());
}

TEST_F(HashNodeTest, RandomValidWriteTest) {
    LOGINFO("INFO: Insert all items in the range of 4");
    uint32_t offset{0};
    while (offset < g_max_offset) {
        const auto sz{g_size_generator(g_re)};
        const koffset_t s{s_cast< koffset_t >(offset)};
        LOGTRACE("Inserting range {} to {} cur_offset={}", s,
                 s + std::min(sz, s_cast< koffset_t >(g_max_offset - offset)), offset);
        insert_range(s, s + std::min(sz, s_cast< koffset_t >(g_max_offset - offset)), true);
        offset += sz + 1;
    }
    validate_all();
    LOGINFO("Node details after all insert: {}", m_node->to_string());

    auto num_iters{SDS_OPTIONS["num_iters"].as< uint64_t >()};
    LOGINFO("INFO: Insert/Erase valid entries randomly for {} iterations", num_iters);
    for (uint64_t i{0}; i < num_iters; ++i) {
        if (m_shadow_map.size() < g_max_offset + 1) {
            const auto [s, e] = pick_to_insert(g_size_generator(g_re));
            LOGTRACE("Inserting [{}-{}]:", s, e);
            insert_range(s, e, true);
            LOGTRACE("After insert node: {}", m_node->to_string());
            m_node->validate_keys();
        }
        if (m_shadow_map.size() > 0) {
            const auto [s, e] = pick_to_erase(g_size_generator(g_re));
            LOGTRACE("Erasing [{}-{}]:", s, e);
            erase_range(s, e, e - s + 1);
            LOGTRACE("After erase node: {}", m_node->to_string());
            m_node->validate_keys();
        }
    }
    LOGINFO("Node details after test: {}", m_node->to_string());
}

TEST_F(HashNodeTest, RandomEverythingTest) {
    enum class op_t : uint8_t { read = 0, insert = 1, erase = 2 };
    uint32_t nread_ops{0}, ninsert_ops{0}, nerase_ops{0};
    uint32_t nblks_read{0}, nblks_inserted{0}, nblks_erased{0};

    auto num_iters{SDS_OPTIONS["num_iters"].as< uint64_t >()};
    LOGINFO("INFO: Do completely random read/insert/erase operations with both valid and invalid entries for {} iters",
            num_iters);
    for (uint64_t i{0}; i < num_iters; ++i) {
        const op_t op{s_cast< op_t >(g_op_generator(g_re))};
        const koffset_t offset{g_offset_generator(g_re)};
        koffset_t size{g_size_generator(g_re)};
        if (g_max_offset - offset + 1 < size) { size = g_max_offset - offset + 1; }

        switch (op) {
        case op_t::read:
            validate_range(offset, offset + size - 1);
            nblks_read += m_inserted_slots.get_set_count(offset, offset + size - 1);
            ++nread_ops;
            break;
        case op_t::insert: {
            auto expected_inserts = size - m_inserted_slots.get_set_count(offset, offset + size - 1);
            insert_range(offset, offset + size - 1, m_inserted_slots.is_bits_reset(offset, size));
            nblks_inserted += expected_inserts;
            ++ninsert_ops;
            break;
        }
        case op_t::erase: {
            auto expected_erases = m_inserted_slots.get_set_count(offset, offset + size - 1);
            erase_range(offset, offset + size - 1, expected_erases);
            nblks_erased += expected_erases;
            ++nerase_ops;
            break;
        }
        }
    }
    LOGINFO("Node details after test: {}", m_node->to_string());
    LOGINFO("Executed read_ops={}, blks_read={} insert_ops={} blks_inserted={} erase_ops={} blks_erased={}", nread_ops,
            nblks_read, ninsert_ops, nblks_inserted, nerase_ops, nblks_erased);
}
#endif

SDS_OPTIONS_ENABLE(logging, test_hashmap)
SDS_OPTION_GROUP(test_hashmap,
                 (max_offset, "", "max_offset", "max number of offset",
                  ::cxxopts::value< uint32_t >()->default_value("65536"), "number"),
                 (num_iters, "", "num_iters", "number of iterations for rand ops",
                  ::cxxopts::value< uint32_t >()->default_value("65536"), "number"))

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SDS_OPTIONS_LOAD(argc, argv, logging, test_hashmap)
    sisl_logging::SetLogger("test_hashmap");
    spdlog::set_pattern("[%D %T%z] [%^%L%$] [%t] %v");

    g_max_offset = SDS_OPTIONS["max_offset"].as< uint32_t >();
    auto ret = RUN_ALL_TESTS();
    return ret;
}
