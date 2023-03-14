/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
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
#include <random>
#include "sisl/fds/buffer.hpp"

SISL_LOGGING_INIT(test_sg_list)
SISL_OPTIONS_ENABLE(logging, test_sg_list)
SISL_OPTION_GROUP(test_sg_list,
                  (num_threads, "", "num_threads", "number of threads",
                   ::cxxopts::value< uint32_t >()->default_value("8"), "number"))

static constexpr uint32_t SZ{sizeof(uint32_t)};

// the iterator request size is same as iov size for each iov;
TEST(SgListTestBasic, TestIteratorAlignedSize) {
    sisl::sg_iovs_t iovs;
    iovs.push_back(iovec{nullptr, 1024});
    iovs.push_back(iovec{nullptr, 512});
    iovs.push_back(iovec{nullptr, 2048});
    iovs.push_back(iovec{nullptr, 512});
    uint32_t iov_size_total = 0;
    for (const auto& v : iovs) {
        iov_size_total += v.iov_len;
    }

    sisl::sg_list sg;
    sg.size = iov_size_total;
    sg.iovs = iovs;

    sisl::sg_iterator sg_it{sg.iovs};
    std::vector< uint32_t > bids_size_vec{1024, 512, 2048, 512};
    uint32_t bids_size_total = 0;
    for (const auto s : bids_size_vec) {
        bids_size_total += s;
    }

    ASSERT_EQ(iov_size_total, bids_size_total);

    uint32_t itr_size_total = 0;
    for (const auto& size : bids_size_vec) {
        const auto iovs = sg_it.next_iovs(size);
        for (const auto& iov : iovs) {
            itr_size_total += iov.iov_len;
        }
    }

    ASSERT_EQ(itr_size_total, bids_size_total);
}

//
// the iterator request size is unaligned with iov len, but total size is same;
//
TEST(SgListTestBasic, TestIteratorUnalignedSize) {
    sisl::sg_iovs_t iovs;
    iovs.push_back(iovec{nullptr, 1024});
    iovs.push_back(iovec{nullptr, 512});
    iovs.push_back(iovec{nullptr, 2048});
    iovs.push_back(iovec{nullptr, 512});
    uint32_t iov_size_total = 0;
    for (const auto& v : iovs) {
        iov_size_total += v.iov_len;
    }

    sisl::sg_list sg;
    sg.size = iov_size_total;
    sg.iovs = iovs;

    sisl::sg_iterator sg_it{sg.iovs};
    std::vector< uint32_t > bids_size_vec{512, 1024, 1024, 512, 512, 512};
    uint32_t bids_size_total = 0;
    for (const auto s : bids_size_vec) {
        bids_size_total += s;
    }

    ASSERT_EQ(iov_size_total, bids_size_total);

    uint32_t itr_size_total = 0;
    for (const auto& size : bids_size_vec) {
        const auto iovs = sg_it.next_iovs(size);
        for (const auto& iov : iovs) {
            itr_size_total += iov.iov_len;
        }
    }

    ASSERT_EQ(itr_size_total, bids_size_total);
}

class SgListTestOffset : public testing::Test {
public:
    void SetUp() override {

        for (uint16_t i = 0; i < 8; ++i) {
            data_vec.emplace_back(get_random_num());
            sgl.iovs.emplace_back(iovec{new uint32_t(data_vec[i]), SZ});
        }
        sgl.size = SZ * 8;
    }

    void TearDown() override {
        for (auto& iov : sgl.iovs) {
            auto data_ptr = r_cast< uint32_t* >(iov.iov_base);
            delete data_ptr;
        }
    }

    static uint32_t get_random_num() {
        static std::random_device dev;
        static std::mt19937 rng(dev());
        std::uniform_int_distribution< std::mt19937::result_type > dist(1001u, 99999u);
        return dist(rng);
    }

    std::vector< uint32_t > data_vec;
    sisl::sg_list sgl{0, {}};
};

TEST_F(SgListTestOffset, TestMoveOffsetAligned) {
    // test next_iovs and sg_list_to_ioblob_list
    sisl::sg_iterator sgitr{sgl.iovs};
    auto ioblob_list = sisl::io_blob::sg_list_to_ioblob_list(sgl);
    ASSERT_EQ(sgl.iovs.size(), ioblob_list.size());
    ASSERT_EQ(sgl.iovs.size(), data_vec.size());
    for (uint16_t i = 0; i < data_vec.size(); ++i) {
        auto const iovs = sgitr.next_iovs(SZ);
        ASSERT_EQ(iovs.size(), 1);
        auto rand_num = r_cast< uint32_t* >(iovs[0].iov_base);
        EXPECT_EQ(*rand_num, data_vec[i]);

        rand_num = r_cast< uint32_t* >(ioblob_list[i].bytes);
        EXPECT_EQ(*rand_num, data_vec[i]);
        EXPECT_EQ(ioblob_list[i].size, SZ);
    }

    sisl::sg_iterator sgitr1{sgl.iovs};
    // test move_offset
    for (uint16_t i = 0; i < data_vec.size(); ++i) {
        if (i % 2 == 0) {
            sgitr1.move_offset(SZ);
            continue;
        }
        auto const iovs = sgitr1.next_iovs(SZ);
        ASSERT_EQ(iovs.size(), 1);
        auto rand_num = r_cast< uint32_t* >(iovs[0].iov_base);
        EXPECT_EQ(*rand_num, data_vec[i]);
    }
}

TEST_F(SgListTestOffset, TestMoveOffsetUnaligned) {
    // total size should be SZ * 8
    std::vector< uint32_t > size_vec{SZ, 3 * SZ, SZ / 2, SZ / 4, 2 * SZ, SZ / 4 + SZ};
    uint32_t itr_size_total{0};
    sisl::sg_iterator sgitr{sgl.iovs};
    for (auto const& s : size_vec) {
        auto const iovs = sgitr.next_iovs(s);
        for (const auto& iov : iovs) {
            itr_size_total += iov.iov_len;
        }
    }
    EXPECT_EQ(itr_size_total, sgl.size);

    sisl::sg_iterator sgitr1{sgl.iovs};
    uint32_t itr_size_offset{0};
    uint32_t itr_size_offset_target{0};
    for (uint16_t i = 0; i < size_vec.size(); ++i) {
        if (i % 2 == 0) {
            sgitr1.move_offset(size_vec[i]);
            continue;
        }
        auto const iovs = sgitr1.next_iovs(size_vec[i]);
        for (const auto& iov : iovs) {
            itr_size_offset += iov.iov_len;
        }
        itr_size_offset_target += size_vec[i];
    }
    EXPECT_EQ(itr_size_offset_target, itr_size_offset);
}

int main(int argc, char* argv[]) {
    int parsed_argc{argc};
    ::testing::InitGoogleTest(&parsed_argc, argv);
    SISL_OPTIONS_LOAD(parsed_argc, argv, logging, test_sg_list);
    sisl::logging::SetLogger("test_sg_list");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    const auto ret{RUN_ALL_TESTS()};
    return ret;
}
