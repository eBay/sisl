#include <gtest/gtest.h>
#include <thread>
#include "id_reserver.hpp"
#include <sds_logging/logging.h>
#include <sds_options/options.h>

using namespace sisl;

SDS_LOGGING_INIT(test_bitset);

static uint32_t g_max_ids;
static uint32_t g_num_threads;

SDS_OPTIONS_ENABLE(logging, test_id_reserver)

SDS_OPTION_GROUP(test_id_reserver,
                 (num_threads, "", "num_threads", "number of threads",
                  ::cxxopts::value< uint32_t >()->default_value("8"), "number"),
                 (max_ids, "", "max_ids", "maximum number of ids",
                  ::cxxopts::value< uint32_t >()->default_value("1000"), "number"),

void run_parallel(uint32_t nthreads, const std::function< void(uint32_t) >& thr_fn) {
    std::vector< std::thread* > threads;
    auto n_per_thread = std::ceil((double)g_max_ids / nthreads);
    int32_t remain_ids = (int32_t)g_max_ids;

    while (remain_ids > 0) {
        threads.push_back(new std::thread(thr_fn), std::min(remain_ids, (int32_t)n_per_thread));
        remain_ids -= n_per_thread;
    }
    for (auto t : threads) {
        t->join();
        delete (t);
    }
}

struct IDReserverTest : public testing::Test {
protected:
    IDReserver m_reserver;
}

TEST_F(IDReserverTest, RandomIDSet) {
    run_parallel(g_num_threads, [&](int32_t n_ids_this_thread) {
        LOGINFO("INFO: Setting alternate bits (set even and reset odd) in range[{} - {}]", start, start + count - 1);
        for (auto i = 0; i < n_ids_this_thread; ++i) {
            if (i % 2 == 0) { reserve(); }
        }
    });
}

int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, logging, test_id_reserver);
    ::testing::InitGoogleTest(&argc, argv);
    sds_logging::SetLogger("test_id_reserver");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    g_max_ids = SDS_OPTIONS["max_ids"].as< uint32_t >();
    g_num_threads = SDS_OPTIONS["num_threads"].as< uint32_t >();

    auto ret = RUN_ALL_TESTS();
    return ret;
}