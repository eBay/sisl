#if defined(JEMALLOC_EXPORT) || defined(USING_JEMALLOC) || defined(USE_JEMALLOC)

#include <cstdint>
#include <iterator>
#include <memory>
#include <thread>

#include <sds_logging/logging.h>
#include <sds_options/options.h>

#include <gtest/gtest.h>

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
};
} // namespace

TEST_F(JemallocTest, GetDirtyPageCount) {
    // allocated/deallocate memory
    {
        static constexpr size_t mem_count{1000000};
        std::unique_ptr< uint64_t[] > mem{new uint64_t[mem_count]};
    }

    const size_t total_page_count{get_jemalloc_dirty_page_count()};
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
    nlohmann::json json_metrics;
    auto& metrics{MallocMetrics::get()};
    get_parse_jemalloc_stats(&json_metrics, &metrics);

    const auto stats_itr{json_metrics.find("Stats")};
    ASSERT_NE(stats_itr, std::end(json_metrics));
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