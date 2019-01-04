//
// Created by Kadayam, Hari on 12/21/18.
//

#include <gtest/gtest.h>
#include "utility/thread_buffer.hpp"
#include "fds/wisr_ds.hpp"
#include <list>
#include <atomic>
#include <thread>

THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

using namespace sisl;
using namespace sisl::fds;

#define INITIAL_THREADS         (6U)
#define ADDLN_THREADS           (2U)
#define ENTRIES_PER_THREAD      (10000U)
#define TOTAL_THREADS           (INITIAL_THREADS + ADDLN_THREADS)
#define TOTAL_ENTRIES           (ENTRIES_PER_THREAD * TOTAL_THREADS)

struct WaitFreeWriteVectorTest : public testing::Test {
protected:
    std::vector< std::thread * > m_threads;
    std::thread* m_scrapper_thread;
    sisl::fds::wisr_vector< uint64_t > m_vec;
    std::atomic< uint32_t > m_write_threads_completed;

    WaitFreeWriteVectorTest() :
           m_vec(1000),
           m_write_threads_completed(0) {
        for(auto i = 0U; i < INITIAL_THREADS; i++) {
            m_threads.push_back(new std::thread(write_thread, i, this));
        }
        m_scrapper_thread = new std::thread(scrapper_thread, this);
    }

    ~WaitFreeWriteVectorTest() {
        for (auto t: m_threads) {
            t->join();
            delete(t);
        }

        m_scrapper_thread->join();
        delete(m_scrapper_thread);
    }

    static void write_thread(uint32_t thread_index, WaitFreeWriteVectorTest *test) {
        uint64_t lhs_mask = (uint64_t)thread_index<<32;
        //uint32_t rhs_val = 0;
        for (auto i = 0U; i < ENTRIES_PER_THREAD; i++) {
            usleep(rand() % ((thread_index + 1) * 100));
            //rhs_val += rand() % 1000 + 1;      // Make sure entry is unique within the thread
            uint64_t val = lhs_mask | i; // Make sure entry is unique across the thread by putting thread as lhs
            test->m_vec.push_back(val);
        }
        std::cout << "Thread " << thread_index << " done writing\n";
        test->m_write_threads_completed++;
    }

    static void scrape(WaitFreeWriteVectorTest *test, std::vector< uint64_t >& result_vec) {
        auto vec_copy = test->m_vec.get_copy_and_reset();
        result_vec.insert(result_vec.end(), vec_copy->begin(), vec_copy->end());
        std::cout << "Scrapped " << vec_copy->size() << " in this iteration. total_entries_recvd so far = "
                  << result_vec.size() << "\n";
    }

    static void scrapper_thread(WaitFreeWriteVectorTest *test) {
        std::vector< uint64_t > final_vec;

        do {
            usleep(50000);
            scrape(test, final_vec);

            // Start any remaining new threads
            for (auto i = test->m_threads.size(); i < TOTAL_THREADS; i++) {
                test->m_threads.push_back(new std::thread(write_thread, i, test));
            }

            if (test->m_write_threads_completed.load() == TOTAL_THREADS) {
                // All threads finished writing, scrape once and check for missing entries
                scrape(test, final_vec);
                find_missing(test, final_vec);
                break;
            }
        } while (true);
    }

    static void find_missing(WaitFreeWriteVectorTest* test, std::vector< uint64_t >& result) {
        std::sort(result.begin(), result.end());
        auto it = result.begin();
        for (auto i = 0U; i < test->m_threads.size(); i++) {
            uint64_t lhs_mask = ((uint64_t)i) << 32;
            for (auto i = 0U; i < ENTRIES_PER_THREAD; i++) {
                uint64_t expected = lhs_mask | i;
                if (expected != *it) {
                    EXPECT_EQ(*it, expected);
                    printf("Missing 0x%lx\n", expected);
                    //std::cout << "Missing " << expected << "\n";
                } else {
                    ++it;
                }
            }
        }
    }
};


TEST_F(WaitFreeWriteVectorTest, insert_parallel_test) {
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}