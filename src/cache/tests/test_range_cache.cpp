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

#ifdef __WIN32
#elif __WIN32
#elif __linux__
#include <fcntl.h>
#include <unistd.h>
#else
#define PSOURCE _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#include <fcntl.h>
#include <unistd.h>
#define _POSIX_C_SOURCE PSOURCE
#undef PSOURCE
#endif

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/utility/enum.hpp>
#include <sisl/cache/range_cache.hpp>
#include <sisl/cache/lru_evictor.hpp>

using namespace sisl;
SISL_LOGGING_INIT(test_rangecache)

static uint32_t g_num_chunks;
static int64_t g_chunk_size;
static constexpr uint32_t g_blk_size{4096};
static thread_local std::random_device g_rd{};
static thread_local std::default_random_engine g_re{g_rd()};

struct RangeCacheTest : public testing::Test {
protected:
    std::shared_ptr< Evictor > m_evictor;
    std::unique_ptr< RangeCache< uint32_t > > m_cache;
    std::vector< int > m_fds;

    uint64_t m_cache_missed_nblks{0};
    uint64_t m_cache_hit_nblks{0};
    uint64_t m_cache_pieces{0};

protected:
    void SetUp() override {
        int64_t cache_size = SISL_OPTIONS["cache_size_mb"].as< uint32_t >() * 1024 * 1024;
        m_evictor = std::make_unique< LRUEvictor >(cache_size, 8);
        m_cache = std::make_unique< RangeCache< uint32_t > >(m_evictor, cache_size / 4096, 4096 /* blk_size */);

        g_num_chunks = SISL_OPTIONS["num_chunks"].as< uint32_t >();
        auto cache_pct = SISL_OPTIONS["cache_pct"].as< uint32_t >();
        g_chunk_size = (cache_size * (100 / cache_pct)) / g_num_chunks;
        LOGINFO("Initializing cache_size={} MB, num_chunks={} each_chunk_size={}",
                SISL_OPTIONS["cache_size_mb"].as< uint32_t >(), g_num_chunks, g_chunk_size);
        file_init(g_num_chunks, g_chunk_size);
    }

    void TearDown() override {
        m_evictor.reset();
        m_cache.reset();
        file_delete(g_num_chunks);
    }

    void write(const uint32_t chunk_num, const uint32_t start_blk, const uint32_t end_blk) {
        uint32_t size = (end_blk - start_blk + 1) * g_blk_size;
        sisl::io_blob b{uintptr_cast(generate_data(size)), size, false};
        file_write(chunk_num, start_blk, b);
        m_cache->insert(chunk_num, start_blk, end_blk - start_blk + 1, std::move(b));
    }

    void read(const uint32_t chunk_num, const uint32_t start_blk, const uint32_t end_blk) {
        auto vec = m_cache->get(chunk_num, start_blk, end_blk - start_blk + 1);
        auto cache_it = vec.begin();

        uint32_t cur_blk = start_blk;
        while (cur_blk <= end_blk) {
            if ((cache_it != vec.end()) && (cache_it->first.m_nth == cur_blk)) {
                // Cache hit of cur_blk, validate it
                validate_blks(chunk_num, *cache_it);
                cur_blk += cache_it->first.m_count;
                m_cache_hit_nblks += cache_it->first.m_count;
                ++m_cache_pieces;
                ++cache_it;
            } else {
                auto nblks = (cache_it != vec.end()) ? cache_it->first.m_nth - cur_blk : end_blk - cur_blk + 1;
                sisl::io_blob file_data = file_read(chunk_num, cur_blk, nblks);
                m_cache->insert(chunk_num, cur_blk, nblks, std::move(file_data));
                m_cache_missed_nblks += nblks;
                cur_blk += nblks;
            }
        }
    }

private:
    void file_init(const uint32_t nchunks, const int64_t chunk_size) {
        for (uint32_t i{1}; i <= nchunks; ++i) {
            int fd;
            auto fname = fmt::format("/tmp/cache_test_file_chunk_{}", i);
            if (!std::filesystem::exists(std::filesystem::path{fname})) {
                LOGINFO("File {} doesn't exists, creating a file for size {}", fname, chunk_size);
                fd = ::open(fname.c_str(), O_RDWR | O_CREAT, 0666);
                ASSERT_NE(fd, -1) << "Open of file " << fname << " failed";
#ifdef __linux__
                const auto ret{fallocate(fd, 0, 0, chunk_size)};
                ASSERT_EQ(ret, 0) << "fallocate of file " << fname << " for size " << chunk_size << " failed";
#elif _WIN32
#elif _WIN64
#else // Assume MacOS
                fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, chunk_size, 0};
                int ret = fcntl(fd, F_PREALLOCATE, &store);
                ASSERT_NE(ret, -1) << "Could not allocate file of size" << chunk_size;
                ftruncate(fd, chunk_size);
#endif
            } else {
                fd = ::open(fname.c_str(), O_RDWR | O_CREAT, 0666);
                ASSERT_NE(fd, -1) << "Open of file " << fname << " failed";
            }
            m_fds.push_back(fd);

            int64_t filled_size{0};
            static constexpr uint32_t max_blk_size = (1 * 1024 * 1024);
            LOGINFO("File {} being filled with random bytes for size={}", fname, chunk_size);
            while (filled_size < chunk_size) {
                uint32_t this_size = std::min(uint32_cast(chunk_size - filled_size), max_blk_size);
                auto size = ::write(fd, generate_data(this_size), this_size);
                ASSERT_EQ(size, this_size);
                filled_size += this_size;
            }
        }
    }

    using random_bytes_engine = std::independent_bits_engine< std::default_random_engine, 64, unsigned long >;
    void* generate_data(const uint32_t buf_size) {
        random_bytes_engine rbe;
        uint64_t* buf = new uint64_t[buf_size / 8];
        for (uint32_t s{0}; s < buf_size / 8; ++s) {
            buf[s] = rbe();
        }
        return r_cast< void* >(buf);
    }

    void file_delete(const uint32_t nchunks) {
        for (auto fd : m_fds) {
            ::close(fd);
        }
        m_fds.clear();
        for (uint32_t i{1}; i <= nchunks; ++i) {
            auto fname = fmt::format("/tmp/cache_test_file_chunk_{}", i);
            LOGINFO("Removing file {}", fname);
            std::filesystem::remove(std::filesystem::path{fname});
        }
    }

    void file_write(const uint32_t chunk_num, const uint32_t start_blk, sisl::io_blob& b) {
        const auto written = ::pwrite(m_fds[chunk_num], voidptr_cast(b.bytes()), b.size(), (start_blk * g_blk_size));
        RELEASE_ASSERT_EQ(written, b.size(), "Not entire data is written to file");
    }

    sisl::io_blob file_read(const uint32_t chunk_num, const uint32_t blk, const uint32_t nblks) {
        sisl::io_blob b{nblks * g_blk_size, 0};
        const auto read_size = ::pread(m_fds[chunk_num], voidptr_cast(b.bytes()), b.size(), (blk * g_blk_size));
        RELEASE_ASSERT_EQ(uint32_cast(read_size), b.size(), "Not entire data is read from file");
        return b;
    }

    void validate_blks(const uint32_t chunk_num, std::pair< RangeKey< uint32_t >, sisl::byte_view >& data) {
        auto b = file_read(chunk_num, data.first.m_nth, data.first.m_count);
        ASSERT_EQ(data.second.size(), data.first.m_count * g_blk_size)
            << "Mismatch of size between byte_view and RangeKey";
        auto ret = ::memcmp(data.second.bytes(), b.bytes(), b.size());
        ASSERT_EQ(ret, 0) << "Data validation failed for Blk [" << data.first.m_nth << "-" << data.first.end_nth()
                          << "]";
        b.buf_free();
    }

#if 0
    void compare_data(const uint32_t offset, const uint8_t* l_bytes, const uint8_t* r_bytes) const {
        const auto l_arr = (std::array< uint32_t, g_blk_size / sizeof(uint32_t) >*)l_bytes;
        const auto r_arr = (std::array< uint32_t, g_blk_size / sizeof(uint32_t) >*)r_bytes;

        for (size_t i{0}; i < l_arr->size(); ++i) {
            ASSERT_EQ(l_arr->at(i), r_arr->at(i)) << "Mismatch of bytes at byte=" << i << " on offset=" << offset;
            ASSERT_EQ(l_arr->at(i), offset) << "Expected data to be same as offset=" << offset;
        }
    }
#endif
};

VENUM(op_t, uint8_t, READ = 0, WRITE = 1)

TEST_F(RangeCacheTest, RandomData) {
    const uint32_t last_blk = g_chunk_size / g_blk_size - 1;
    static std::uniform_int_distribution< uint32_t > nblks_generator{1, 2048};
    static std::uniform_int_distribution< uint8_t > op_generator{0, 1};
    static std::uniform_int_distribution< uint32_t > chunk_generator{0, g_num_chunks - 1};
    static std::uniform_int_distribution< uint32_t > blk_generator{0, last_blk};

    uint32_t nblks_read{0};
    uint32_t nblks_written{0};
    uint32_t nread_ops{0};
    uint32_t nwrite_ops{0};

    auto num_iters = SISL_OPTIONS["num_iters"].as< uint32_t >();
    LOGINFO("INFO: Do random read/write operations on all chunks for {} iters", num_iters);
    for (uint32_t i{0}; i < num_iters; ++i) {
        const op_t op = s_cast< op_t >(op_generator(g_re));
        const auto chunk_num = chunk_generator(g_re);
        const uint32_t start_blk = blk_generator(g_re);
        uint32_t nblks = nblks_generator(g_re);
        if (nblks + start_blk > last_blk) { nblks = last_blk - start_blk; }

        LOGINFO("INFO: Doing op={} on chunk={} for blks=[{}-{}]", enum_name(op), chunk_num, start_blk,
                start_blk + nblks - 1);
        switch (op) {
        case op_t::READ:
            read(chunk_num, start_blk, start_blk + nblks - 1);
            nblks_read += nblks;
            ++nread_ops;
            break;
        case op_t::WRITE:
            write(chunk_num, start_blk, start_blk + nblks - 1);
            nblks_written += nblks;
            ++nwrite_ops;
            break;
        }
    }
    LOGINFO("Executed read_ops={}, blks_read={} write_ops={} blks_written={}", nread_ops, nblks_read, nwrite_ops,
            nblks_written);
    LOGINFO("Cache hits={} ({}%) Cache Misses={} ({}%), Avg pieces per cache hit entry={}", m_cache_hit_nblks,
            (100 * m_cache_hit_nblks) / nblks_read, m_cache_missed_nblks, (100 * m_cache_missed_nblks) / nblks_read,
            m_cache_hit_nblks / m_cache_pieces);
}

SISL_OPTIONS_ENABLE(logging, test_rangecache)
SISL_OPTION_GROUP(test_rangecache,
                  (cache_size_mb, "", "cache_size_mb", "cache size in mb",
                   ::cxxopts::value< uint32_t >()->default_value("100"), "number"),
                  (cache_pct, "", "cache_pct", "percentage of cache",
                   ::cxxopts::value< uint32_t >()->default_value("50"), "number"),
                  (num_chunks, "", "num_chunks", "Total number of chunks",
                   ::cxxopts::value< uint32_t >()->default_value("8"), "number"),
                  (num_iters, "", "num_iters", "number of iterations for rand ops",
                   ::cxxopts::value< uint32_t >()->default_value("65536"), "number"))

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging, test_rangecache)
    sisl::logging::SetLogger("test_rangecache");
    spdlog::set_pattern("[%D %T%z] [%^%L%$] [%t] %v");

    auto ret = RUN_ALL_TESTS();
    return ret;
}
