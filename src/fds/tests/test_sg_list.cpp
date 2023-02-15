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
#include "sisl/fds/buffer.hpp"

SISL_LOGGING_INIT(test_sg_list)
SISL_OPTIONS_ENABLE(logging, test_sg_list)
SISL_OPTION_GROUP(test_sg_list,
                  (num_threads, "", "num_threads", "number of threads",
                   ::cxxopts::value< uint32_t >()->default_value("8"), "number"))

struct SgListTest : public testing::Test {};

// the iterator request size is same as iov size for each iov;
TEST_F(SgListTest, TestIteratorAlignedSize) {

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

    assert(iov_size_total == bids_size_total);

    uint32_t itr_size_total = 0;
    for (const auto& size : bids_size_vec) {
        const auto iovs = sg_it.next_iovs(size);
        for (const auto& iov : iovs) {
            itr_size_total += iov.iov_len;
        }
    }

    assert(itr_size_total == bids_size_total);
}

//
// the iterator request size is unaligned with iov len, but total size is same;
//
TEST_F(SgListTest, TestIteratorUnalignedSize) {
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

    assert(iov_size_total == bids_size_total);

    uint32_t itr_size_total = 0;
    for (const auto& size : bids_size_vec) {
        const auto iovs = sg_it.next_iovs(size);
        for (const auto& iov : iovs) {
            itr_size_total += iov.iov_len;
        }
    }

    assert(itr_size_total == bids_size_total);
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
