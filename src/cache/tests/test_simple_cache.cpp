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
#include <gtest/gtest.h>
#include <string>
#include <random>
#include <filesystem>
#include <cstdint>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/utility/enum.hpp>
#include <sisl/cache/simple_cache.hpp>
#include <sisl/cache/lru_evictor.hpp>

using namespace sisl;
REGISTER_LOG_MOD(test_simplecache)

static constexpr uint32_t g_val_size{512};
static thread_local std::random_device g_rd{};
static thread_local std::default_random_engine g_re{g_rd()};

struct Entry {
    Entry(uint32_t id, const std::string& contents = "") : m_id{id}, m_contents{contents} {}

    uint32_t m_id;
    std::string m_contents;
};

static constexpr std::array< const char, 62 > alphanum{
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K',
    'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z'};

static std::string gen_random_string(size_t len) {
    std::string str;
    static thread_local std::random_device rd{};
    static thread_local std::default_random_engine re{rd()};
    std::uniform_int_distribution< size_t > rand_char{0, alphanum.size() - 1};
    for (size_t i{0}; i < len; ++i) {
        str += alphanum[rand_char(re)];
    }
    str += '\0';
    return str;
}

struct SimpleCacheTest : public testing::Test {
protected:
    std::shared_ptr< Evictor > m_evictor;
    std::unique_ptr< SimpleCache< uint32_t, std::shared_ptr< Entry > > > m_cache;
    std::unordered_map< uint32_t, std::string > m_shadow_map;

    uint64_t m_cache_misses{0};
    uint64_t m_cache_hits{0};
    uint32_t m_total_keys;

protected:
    void SetUp() override {
        const auto cache_size = SISL_OPTIONS["cache_size_mb"].as< uint32_t >() * 1024 * 1024;
        m_evictor = std::make_unique< LRUEvictor >(cache_size, 8);
        m_cache = std::make_unique< SimpleCache< uint32_t, std::shared_ptr< Entry > > >(
            m_evictor,                                                             // Evictor to evict used entries
            cache_size / 4096,                                                     // Total number of buckets
            g_val_size,                                                            // Value size
            [](const std::shared_ptr< Entry >& e) -> uint32_t { return e->m_id; }, // Method to extract key
            nullptr                                                                // Method to prevent eviction
        );

        const auto cache_pct = SISL_OPTIONS["cache_pct"].as< uint32_t >();
        const auto total_data_size = (100 * cache_size) / cache_pct;
        m_total_keys = total_data_size / g_val_size;
        LOGINFO("Initializing cache_size={} MB, cache_pct={}, total_data_size={}",
                SISL_OPTIONS["cache_size_mb"].as< uint32_t >(), cache_pct, total_data_size);
    }

    void TearDown() override {
        m_evictor.reset();
        m_cache.reset();
    }

    void write(uint32_t id) {
        const std::string data = gen_random_string(g_val_size);
        const auto [it, expected_insert] = m_shadow_map.insert_or_assign(id, data);

        bool inserted = m_cache->upsert(std::make_shared< Entry >(id, data));
        ASSERT_EQ(inserted, expected_insert)
            << "Mismatch about existence of key=" << id << " between shadow_map and cache";
    }

    void read(uint32_t id) {
        const auto it = m_shadow_map.find(id);
        bool expected_found = (it != m_shadow_map.end());

        std::shared_ptr< Entry > e = std::make_shared< Entry >(0);
        bool found = m_cache->get(id, e);
        if (found) {
            ASSERT_EQ(expected_found, true) << "Object key=" << id << " is deleted, but still found in cache";
            ASSERT_EQ(e->m_contents, it->second) << "Contents for key=" << id << " mismatch";
            ++m_cache_hits;
        } else if (expected_found) {
            bool inserted = m_cache->insert(std::make_shared< Entry >(id, it->second));
            ASSERT_EQ(inserted, true) << "Unable to insert to the cache for key=" << id;
            ++m_cache_misses;
        }
    }

    void remove(uint32_t id) {
        const auto it = m_shadow_map.find(id);
        bool expected_found = (it != m_shadow_map.end());

        std::shared_ptr< Entry > removed_e = std::make_shared< Entry >(0);
        bool removed = m_cache->remove(id, removed_e);
        if (removed) {
            ASSERT_EQ(expected_found, true)
                << "Object for key=" << id << " is deleted already, but still found in cache";
            ASSERT_EQ(removed_e->m_contents, it->second) << "Contents for key=" << id << " mismatch prior to removal";
            ++m_cache_hits;
        } else {
            ++m_cache_misses;
        }

        m_shadow_map.erase(id);
    }
};

VENUM(op_t, uint8_t, READ = 0, WRITE = 1, REMOVE = 2)

TEST_F(SimpleCacheTest, RandomData) {
    static std::uniform_int_distribution< uint8_t > op_generator{0, 2};
    static std::uniform_int_distribution< uint32_t > key_generator{0, this->m_total_keys};

    uint32_t nread_ops{0};
    uint32_t nwrite_ops{0};
    uint32_t nremove_ops{0};

    auto num_iters = SISL_OPTIONS["num_iters"].as< uint32_t >();
    LOGINFO("INFO: Do random read/write operations on all chunks for {} iters", num_iters);
    for (uint32_t i{0}; i < num_iters; ++i) {
        const op_t op = s_cast< op_t >(op_generator(g_re));
        const uint32_t id = key_generator(g_re);

        LOGDEBUG("INFO: Doing op={} for key=({})", enum_name(op), id);
        switch (op) {
        case op_t::READ:
            read(id);
            ++nread_ops;
            break;
        case op_t::WRITE:
            write(id);
            ++nwrite_ops;
            break;
        case op_t::REMOVE:
            remove(id);
            ++nremove_ops;
            break;
        }
    }
    const auto cache_ops = m_cache_hits + m_cache_misses;
    LOGINFO("Executed read_ops={}, write_ops={} remove_ops={}", nread_ops, nwrite_ops, nremove_ops);
    LOGINFO("Cache hits={} ({}%) Cache Misses={} ({}%)", m_cache_hits, (100 * (double)m_cache_hits) / cache_ops,
            m_cache_misses, (100 * (double)m_cache_misses) / cache_ops);
}

SISL_OPTIONS_ENABLE(logging, test_simplecache)
SISL_OPTION_GROUP(test_simplecache,
                  (cache_size_mb, "", "cache_size_mb", "cache size in mb",
                   ::cxxopts::value< uint32_t >()->default_value("100"), "number"),
                  (cache_pct, "", "cache_pct", "percentage of cache",
                   ::cxxopts::value< uint32_t >()->default_value("50"), "number"),
                  (num_iters, "", "num_iters", "number of iterations for rand ops",
                   ::cxxopts::value< uint32_t >()->default_value("65536"), "number"))

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging, test_simplecache)
    sisl::logging::SetLogger("test_simplecache");
    spdlog::set_pattern("[%D %T%z] [%^%L%$] [%t] %v");

    auto ret = RUN_ALL_TESTS();
    return ret;
}
