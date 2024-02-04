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
#include <cstdint>
#include <iostream>
#include <boost/dynamic_bitset.hpp>
#include <random>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include <gtest/gtest.h>

#include <sisl/fds/concurrent_insert_vector.hpp>
#include <sisl/fds/bitset.hpp>

using namespace sisl;

SISL_OPTIONS_ENABLE(logging, test_concurrent_insert_vector)

class ConcurrentInsertVectorTest : public testing::Test {
protected:
    ConcurrentInsertVector< uint32_t > m_cvec;
    std::vector< std::thread > m_threads;

public:
    ConcurrentInsertVectorTest() : testing::Test() {}
    ConcurrentInsertVectorTest(const ConcurrentInsertVectorTest&) = delete;
    ConcurrentInsertVectorTest(ConcurrentInsertVectorTest&&) noexcept = delete;
    ConcurrentInsertVectorTest& operator=(const ConcurrentInsertVectorTest&) = delete;
    ConcurrentInsertVectorTest& operator=(ConcurrentInsertVectorTest&&) noexcept = delete;
    virtual ~ConcurrentInsertVectorTest() override = default;

protected:
    void insert_and_wait() {
        auto const nthreads = SISL_OPTIONS["num_threads"].as< uint32_t >();
        auto const per_thread_count = SISL_OPTIONS["num_entries"].as< uint32_t >() / nthreads;
        for (size_t i{0}; i < nthreads; ++i) {
            m_threads.emplace_back(
                [this](uint32_t start, uint32_t count) {
                    for (uint32_t i{0}; i < count; ++i) {
                        m_cvec.push_back(start + i);
                    }
                },
                i * per_thread_count, per_thread_count);
        }

        for (auto& thr : m_threads) {
            thr.join();
        }
    }

    void validate_all() {
        sisl::Bitset bset{SISL_OPTIONS["num_entries"].as< uint32_t >()};
        m_cvec.foreach_entry([&bset](uint32_t const& e) { bset.set_bit(e); });
        ASSERT_EQ(bset.get_next_reset_bit(0), sisl::Bitset::npos) << "Access didn't receive all entries";
        ASSERT_EQ(m_cvec.size(), bset.get_set_count(0)) << "Size doesn't match with number of entries";
    }

    void validate_all_by_iteration() {
        sisl::Bitset bset{SISL_OPTIONS["num_entries"].as< uint32_t >()};
        for (const auto& e : m_cvec) {
            bset.set_bit(e);
        }
        ASSERT_EQ(bset.get_next_reset_bit(0), sisl::Bitset::npos) << "Access didn't receive all entries";
        ASSERT_EQ(m_cvec.size(), bset.get_set_count(0)) << "Size doesn't match with number of entries";
    }
};

TEST_F(ConcurrentInsertVectorTest, concurrent_insertion) {
    LOGINFO("Step1: Inserting {} entries in parallel in {} threads and wait",
            SISL_OPTIONS["num_entries"].as< uint32_t >(), SISL_OPTIONS["num_threads"].as< uint32_t >());
    insert_and_wait();

    LOGINFO("Step2: Validating all entries are inserted");
    validate_all();

    LOGINFO("Step3: Validating all entries again to ensure it is readable multipled times");
    validate_all();

    LOGINFO("Step4: Validating all entries by iterator");
    validate_all_by_iteration();

    LOGINFO("Step5: Validating all entries again by iterator to ensure it is readable multipled times");
    validate_all_by_iteration();
}

SISL_OPTION_GROUP(test_concurrent_insert_vector,
                  (num_entries, "", "num_entries", "num_entries",
                   ::cxxopts::value< uint32_t >()->default_value("10000"), "number"),
                  (num_threads, "", "num_threads", "num_threads", ::cxxopts::value< uint32_t >()->default_value("8"),
                   "number"))

int main(int argc, char* argv[]) {
    int parsed_argc{argc};
    ::testing::InitGoogleTest(&parsed_argc, argv);
    SISL_OPTIONS_LOAD(parsed_argc, argv, logging, test_concurrent_insert_vector);

    sisl::logging::SetLogger("test_concurrent_insert_vector");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    return RUN_ALL_TESTS();
}
