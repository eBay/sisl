//
// Created by Kadayam, Hari on 12/21/18.
//

#include <gtest/gtest.h>
#include "utility/thread_buffer.hpp"
#include <list>
#include <atomic>
#include <thread>

THREAD_BUFFER_INIT;
RCU_REGISTER_INIT;

using namespace sisl;

struct MyList {
    std::list< uint64_t > m_list;
    void add(MyList &other) {
        other.m_list.sort();
        m_list.merge(other.m_list);
    }
};

#define INITIAL_THREADS         (6U)
#define ADDLN_THREADS           (2U)
#define ENTRIES_PER_THREAD      (10000U)
#define TOTAL_THREADS           (INITIAL_THREADS + ADDLN_THREADS)
#define TOTAL_ENTRIES           (ENTRIES_PER_THREAD * TOTAL_THREADS)

struct ThreadBufferTest : public testing::Test {
protected:
    std::vector< std::thread * > m_threads;
    std::thread* m_scrapper_thread;
    sisl::ExitSafeThreadBuffer< MyList > m_buffer;

    ThreadBufferTest() {
        for(auto i = 0U; i < INITIAL_THREADS; i++) {
            m_threads.push_back(new std::thread(write_thread, i, this));
        }
        m_scrapper_thread = new std::thread(scrapper_thread, this);
    }

    ~ThreadBufferTest() {
        for (auto t: m_threads) {
            t->join();
            delete(t);
        }

        m_scrapper_thread->join();
        delete(m_scrapper_thread);
    }

    static void write_thread(uint32_t thread_index, ThreadBufferTest *test) {
        uint64_t lhs_mask = (uint64_t)thread_index<<32;
        //uint32_t rhs_val = 0;
        for (auto i = 0U; i < ENTRIES_PER_THREAD; i++) {
            usleep(rand() % ((thread_index + 1) * 100));
            //rhs_val += rand() % 1000 + 1;      // Make sure entry is unique within the thread
            uint64_t val = lhs_mask | i; // Make sure entry is unique across the thread by putting thread as lhs
            test->m_buffer->m_list.push_back(val);
        }
        std::cout << "Thread " << thread_index << " done writing\n";
    }

    static void scrapper_thread(ThreadBufferTest *test) {
        uint64_t total_entries_recvd = 0;
        MyList merge_list;

        do {
            usleep(50000);
            test->m_buffer.access_all_threads([&merge_list](MyList *ml, bool is_thread_running) {
                merge_list.add(*ml);
                return true;
            });

            auto before_size = merge_list.m_list.size();
            merge_list.m_list.unique();
            ASSERT_EQ(before_size, merge_list.m_list.size());
            total_entries_recvd += merge_list.m_list.size();

            std::cout << "Scrapped " << merge_list.m_list.size() << " in this iteration. total_entries_recvd so far = "
                << total_entries_recvd << "\n";

            // Start any remaining new threads
            for (auto i = test->m_threads.size(); i < TOTAL_THREADS; i++) {
                test->m_threads.push_back(new std::thread(write_thread, i, test));
            }
        } while (total_entries_recvd < TOTAL_ENTRIES);

        std::cout << "Scrapped all entries, total_entries_recvd = " << total_entries_recvd << "\n";
        merge_list.m_list.sort();
        auto it = merge_list.m_list.begin();
        for (auto i = 0U; i < test->m_threads.size(); i++) {
            uint64_t lhs_mask = ((uint64_t)i) << 32;
            for (auto i = 0U; i < ENTRIES_PER_THREAD; i++) {
                auto expected = lhs_mask | i;
                ASSERT_EQ(*it, expected);
                ++it;
            }
        }
    }
};


TEST_F(ThreadBufferTest, insert_parallel_test) {
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}