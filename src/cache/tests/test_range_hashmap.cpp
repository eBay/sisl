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
#include <iostream>
#include <sisl/options/options.h>
#include <gtest/gtest.h>
#include <string>
#include <random>

#include <sisl/logging/logging.h>
#include <sisl/utility/enum.hpp>
#include "sisl/fds/bitset.hpp"
#include <sisl/cache/range_hashmap.hpp>

using namespace sisl;
SISL_LOGGING_INIT(test_hashmap)

static uint32_t g_max_offset{UINT16_MAX};
static constexpr uint32_t per_val_size = 128;

static thread_local std::random_device g_rd{};
static thread_local std::default_random_engine g_re{g_rd()};
static std::uniform_int_distribution< uint64_t > g_rand_generator{1000, 10000};
static std::uniform_int_distribution< big_count_t > g_size_generator{1, 2048};
static std::uniform_int_distribution< uint8_t > g_op_generator{0, 2};

struct RangeHashMapTest : public testing::Test {
protected:
    std::unique_ptr< RangeHashMap< uint32_t > > m_map;
    std::unordered_map< uint32_t, sisl::io_blob > m_shadow_map;
    sisl::Bitset m_inserted_slots{g_max_offset};

protected:
    void SetUp() override { m_map = std::make_unique< RangeHashMap< uint32_t > >(1000, extract_value, nullptr); }

    void TearDown() override {
        for (auto& [k, v] : m_shadow_map) {
            v.buf_free();
        }
    }

    void insert_range(const uint32_t start, const uint32_t end) {
        auto range_blob = create_data(start, end);
        m_map->insert(RangeKey{1u, start, end - start + 1}, range_blob);
        range_blob.buf_free();

        for (auto i{start}; i <= end; ++i) {
            auto new_blob = create_data(i, i);
            auto it = m_shadow_map.find(i);
            if (it != m_shadow_map.end()) {
                it->second.buf_free();
                it->second = new_blob;
            } else {
                m_shadow_map.emplace(i, new_blob);
            }
            m_inserted_slots.set_bit(i);
        }
    }

    void validate_range(const uint32_t start, const uint32_t end) const {
        auto entries = m_map->get(RangeKey{1u, start, end - start + 1});

        for (const auto& [key, val] : entries) {
            ASSERT_EQ(key.m_base_key, 1u) << "Expected base key is standard value 1";
            uint8_t const* got_bytes = val.bytes();
            for (auto o{key.m_nth}; o < key.m_nth + key.m_count; ++o) {
                auto it = m_shadow_map.find(o);
                ASSERT_EQ(m_inserted_slots.is_bits_set(o, 1), true) << "Found a key " << o << " which was not inserted";
                compare_data(o, got_bytes, it->second.cbytes());
                got_bytes += per_val_size;
            }
        }
    }

    void validate_all() { validate_range(0, g_max_offset - 1); }

    void erase_range(const uint32_t start, const uint32_t end) {
        m_map->erase(RangeKey{1u, start, end - start + 1});

        for (auto i{start}; i <= end; ++i) {
            auto it = m_shadow_map.find(i);
            if (it != m_shadow_map.end()) {
                it->second.buf_free();
                m_shadow_map.erase(it);
            }
            m_inserted_slots.reset_bit(i);
        }
    }

    static sisl::byte_view extract_value(const sisl::byte_view& inp_bytes, uint32_t nth, uint32_t count) {
        return sisl::byte_view{inp_bytes, nth * per_val_size, count * per_val_size};
    }

    sisl::io_blob create_data(const uint32_t start, const uint32_t end) {
        auto blob = sisl::io_blob{per_val_size * (end - start + 1), 0};
        uint8_t* bytes = blob.bytes();

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

ENUM(op_t, uint8_t, GET = 0, INSERT = 1, ERASE = 2)

TEST_F(RangeHashMapTest, RandomEverythingTest) {
    uint32_t nread_ops{0}, ninsert_ops{0}, nerase_ops{0};
    uint32_t nblks_read{0}, nblks_inserted{0}, nblks_erased{0};

    static std::uniform_int_distribution< big_offset_t > offset_generator{0, g_max_offset - 1};

    auto num_iters = SISL_OPTIONS["num_iters"].as< uint32_t >();
    LOGINFO("INFO: Do completely random read/insert/erase operations for {} entries for {} iters", g_max_offset,
            num_iters);
    for (uint32_t i{0}; i < num_iters; ++i) {
        const op_t op = static_cast< op_t >(g_op_generator(g_re));
        const big_offset_t offset = offset_generator(g_re);
        big_count_t size = g_size_generator(g_re);
        if (size + offset >= g_max_offset) { size = g_max_offset - offset - 1; }
        if (size == 0) { continue; }

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

// Covers RangeKey::compute_hash() and RangeKey::operator< (lines 56-77 in range_hashmap.hpp).
// These are never called by the map itself (which uses its own static compute_hash); must invoke directly.
TEST(RangeKeyTest, HashAndOrdering) {
    RangeKey< uint32_t > k1{1u, 100, 50};
    RangeKey< uint32_t > k2{1u, 200, 50};
    RangeKey< uint32_t > k3{2u, 100, 50};
    RangeKey< uint32_t > k4{1u, 100, 60};
    RangeKey< uint32_t > k5{1u, 100, 50}; // same as k1

    // compute_hash: same key -> same hash
    EXPECT_EQ(k1.compute_hash(), k5.compute_hash());
    // different nth -> different hash
    EXPECT_NE(k1.compute_hash(), k2.compute_hash());

    // operator<: compare by base_key, then nth, then count
    EXPECT_TRUE(k1 < k2); // same base, lower nth
    EXPECT_FALSE(k2 < k1);
    EXPECT_TRUE(k1 < k3); // lower base_key
    EXPECT_FALSE(k3 < k1);
    EXPECT_TRUE(k1 < k4); // same base+nth, lower count
    EXPECT_FALSE(k4 < k1);
    EXPECT_FALSE(k1 < k5); // equal
    EXPECT_FALSE(k5 < k1);
}

// Covers call_access_cb invocation (lines 129-130, 315-319, 372-376 in range_hashmap.hpp).
// The existing tests pass nullptr for the callback; here we provide a real one.
TEST(RangeHashMapCbTest, AccessCallbackInvoked) {
    struct CallRecord {
        hash_op_t op;
        uint32_t nth;
        int64_t new_size;
    };
    std::vector< CallRecord > calls;

    auto cb = [&calls](const ValueEntryBase&, const RangeKey< uint32_t >& key, const hash_op_t op, int64_t new_size) {
        calls.push_back({op, static_cast< uint32_t >(key.m_nth), new_size});
    };

    constexpr uint32_t per_val = 128;
    auto extractor = [](const sisl::byte_view& inp, big_offset_t nth, big_count_t count) -> sisl::byte_view {
        return sisl::byte_view{inp, nth * per_val, count * per_val};
    };

    RangeHashMap< uint32_t > map{64, extractor, cb};

    auto make_blob = [&](uint32_t start, uint32_t end) {
        sisl::io_blob blob{per_val * (end - start + 1), 0};
        std::fill(blob.bytes(), blob.bytes() + blob.size(), 0xAB);
        return blob;
    };

    // INSERT creates a new entry -> CREATE callback
    auto b1 = make_blob(0, 9);
    map.insert(RangeKey< uint32_t >{1u, 0, 10}, b1);
    b1.buf_free();

    // INSERT overlapping an existing entry: triggers RESIZE on the shrunk portion and CREATE for new
    auto b2 = make_blob(5, 14);
    map.insert(RangeKey< uint32_t >{1u, 5, 10}, b2);
    b2.buf_free();

    EXPECT_FALSE(calls.empty());

    // ERASE: triggers DELETE callback for entries being removed
    size_t before_erase = calls.size();
    map.erase(RangeKey< uint32_t >{1u, 0, 15});
    EXPECT_GT(calls.size(), before_erase);
}

SISL_OPTIONS_ENABLE(logging, test_hashmap)
SISL_OPTION_GROUP(test_hashmap,
                  (max_offset, "", "max_offset", "max number of offset",
                   ::cxxopts::value< uint32_t >()->default_value("65536"), "number"),
                  (num_iters, "", "num_iters", "number of iterations for rand ops",
                   ::cxxopts::value< uint32_t >()->default_value("65536"), "number"))

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging, test_hashmap)
    sisl::logging::SetLogger("test_hashmap");
    spdlog::set_pattern("[%D %T%z] [%^%L%$] [%t] %v");

    g_max_offset = SISL_OPTIONS["max_offset"].as< uint32_t >();
    auto ret = RUN_ALL_TESTS();
    return ret;
}
