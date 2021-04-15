#if defined(JEMALLOC_EXPORT) || defined(USING_JEMALLOC) || defined(USE_JEMALLOC)

#include <cstdint>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>

#include <sds_logging/logging.h>
#include <sds_options/options.h>

#include <gtest/gtest.h>

#include "utility/thread_buffer.hpp"

#include "malloc_helper.hpp"

using namespace sisl;

SDS_LOGGING_INIT(test_jemalloc)
THREAD_BUFFER_INIT

namespace {
uint32_t g_num_threads;

struct JemallocTest : public testing::Test {
public:
    JemallocTest() : testing::Test{} { LOGINFO("Initializing new JemallocTest class"); }
    JemallocTest(const JemallocTest&) = delete;
    JemallocTest(JemallocTest&&) noexcept = delete;
    JemallocTest& operator=(const JemallocTest&) = delete;
    JemallocTest& operator=(JemallocTest&&) noexcept = delete;
    virtual ~JemallocTest() override = default;

protected:
    void SetUp() override {}
    void TearDown() override {}

    void MultiThreadedAllocDealloc(const size_t iterations, const size_t mem_count = 1000000) const {
        const auto thread_lambda{[&iterations, &mem_count]() {
            // allocated/deallocate memory
            for (size_t iteration{0}; iteration < iterations; ++iteration) {
                std::unique_ptr< uint64_t[] > mem{new uint64_t[mem_count]};
            }
        }};

        std::vector< std::thread > threads;
        for (uint32_t thread_num{0}; thread_num < g_num_threads; ++thread_num) {
            threads.emplace_back(thread_lambda);
        }

        for (auto& alloc_dealloc_thread : threads) {
            if (alloc_dealloc_thread.joinable()) alloc_dealloc_thread.join();
        };
    }
};
} // namespace

TEST_F(JemallocTest, SetBackgroundThreads) {
    ASSERT_TRUE(set_jemalloc_background_threads(false)); 
    ASSERT_TRUE(set_jemalloc_background_threads(true));
}

TEST_F(JemallocTest, SetDecayOptions) { ASSERT_TRUE(set_jemalloc_decay_times()); }

TEST_F(JemallocTest, GetDirtyPageCount) {
    MultiThreadedAllocDealloc(100);

    const size_t total_page_count{get_jemalloc_dirty_page_count()};
    ASSERT_GE(total_page_count, static_cast< size_t >(0));
}

TEST_F(JemallocTest, GetMuzzyPageCount) {
    MultiThreadedAllocDealloc(100);

    const size_t total_page_count{get_jemalloc_muzzy_page_count()};
    ASSERT_GE(total_page_count, static_cast< size_t >(0));
}

TEST_F(JemallocTest, GetTotalMemory) {
    static constexpr size_t mem_count{1000000};
    std::unique_ptr< uint64_t[] > mem{new uint64_t[mem_count]};

    // call without refresh
    const size_t total_memory1{get_total_memory(false)};
    ASSERT_GT(total_memory1, static_cast< size_t >(0));

    // call with refresh
    const size_t total_memory2{get_total_memory(true)};
    ASSERT_GT(total_memory2, static_cast< size_t >(0));
}

TEST_F(JemallocTest, GetJSONStatsDetailed) { 
    // must use operator= construction as copy construction results in error
    const nlohmann::json json_stats = get_malloc_stats_detailed(); 
    const auto stats_itr{json_stats.find("Stats")};
    ASSERT_NE(stats_itr, std::cend(json_stats));
}

TEST_F(JemallocTest, GetMetrics) {
    auto& metrics{MallocMetrics::get()};

    // no refresh
    nlohmann::json json_metrics1;
    get_parse_jemalloc_stats(&json_metrics1, &metrics, false);
    const auto stats_itr1{json_metrics1.find("Stats")};
    ASSERT_NE(stats_itr1, std::end(json_metrics1));

    // refresh
    nlohmann::json json_metrics2;
    get_parse_jemalloc_stats(&json_metrics2, &metrics, true);
    const auto stats_itr2{json_metrics2.find("Stats")};
    ASSERT_NE(stats_itr2, std::end(json_metrics2));
}

SDS_OPTIONS_ENABLE(logging, test_jemalloc)

SDS_OPTION_GROUP(test_jemalloc,
                 (num_threads, "", "num_threads", "number of threads",
                  ::cxxopts::value< uint32_t >()->default_value("8"), "number"))

int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging, test_jemalloc);
    ::testing::InitGoogleTest(&argc, argv);
    sds_logging::SetLogger("test_bitset");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    g_num_threads = SDS_OPTIONS["num_threads"].as< uint32_t >();

    const auto ret{RUN_ALL_TESTS()};
    return ret;
}

#endif