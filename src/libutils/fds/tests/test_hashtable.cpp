/*
 * test_hashtable.cpp
 *
 *  Created on: 29-Apr-2017
 *      Author: hkadayam
 */
#include <iostream>
#include <vector>
#include <memory>
#include "libutils/fds/hash/hashset.hpp"

using namespace std;
#define MAX_KEY_LEN 16
class TestEntry : public fds::HashNode {
  public:
    TestEntry(const char* str) : fds::HashNode(), m_str(str), m_key((uint8_t*)m_str.c_str(), m_str.size()) {}

    virtual ~TestEntry() {
        // assert(0);
    }

    virtual fds::LFHashKey* extract_key() { return &m_key; }

  public:
    std::string m_str;

  private:
    fds::LFHashKey m_key;
};

/////////// Workload structures //////////
struct WorkloadInfo {
    uint32_t start;
    uint32_t count;
    uint64_t time_ns;
    uint32_t actual_count;
};

typedef struct {
    fds::HashSet* hs;
    WorkloadInfo preloadInfo;
    WorkloadInfo insertInfo;
    WorkloadInfo readInfo;
    WorkloadInfo deleteInfo;

    pthread_t tid;
    uint32_t myid;
    uint32_t riRatio;
} threadarg_t;

struct HashSetTest {
    std::vector<std::unique_ptr<TestEntry>> preloadEntries;
    std::vector<std::unique_ptr<TestEntry>> insertEntries;
    std::vector<TestEntry*> rawPreloadEntries;
};

HashSetTest tst;

#define NBUCKETS 100
#define NENTRIES 500
#define NVALUES 500

/////////// Initialization structures //////////
void initEntries(uint32_t nPreloadEntries, uint32_t nInsertEntries, uint32_t nUniques) {
    for (auto i = 0; i < nPreloadEntries; i++) {
        char str[MAX_KEY_LEN];
        // sprintf(str, "%d", rand() % nUniques);
        // sprintf(str, "pre-%d", i);
        sprintf(str, "pre-%d", rand());
        tst.preloadEntries.push_back(std::make_unique<TestEntry>(str));
        tst.rawPreloadEntries.push_back(nullptr);
    }

    for (auto i = 0; i < nInsertEntries; i++) {
        char str[MAX_KEY_LEN];
        // sprintf(str, "%d", rand() % nUniques);
        // sprintf(str, "pre-%d", i);
        sprintf(str, "ins-%d", rand());
        tst.insertEntries.push_back(std::make_unique<TestEntry>(str));
    }
}

void* preloadThread(void* arg) {
    threadarg_t* targ = (threadarg_t*)arg;

    for (auto i = targ->preloadInfo.start; i < (targ->preloadInfo.start + targ->preloadInfo.count); i++) {
        TestEntry* te;

        Clock::time_point startTime = Clock::now();
        std::unique_ptr<TestEntry> backe;
        bool inserted = targ->hs->insert(std::move(tst.preloadEntries[i]), (fds::HashNode**)&tst.rawPreloadEntries[i],
                                         (std::unique_ptr<fds::HashNode>*)&backe);
        TestEntry* e1 = tst.rawPreloadEntries[i];
        targ->preloadInfo.time_ns += get_elapsed_time_ns(startTime);

        if (inserted) {
            targ->preloadInfo.actual_count++;
        } else {
            tst.preloadEntries[i] = std::move(backe);
        }

        if (((i + 1) % 1000) == 0) {
            printf("Thread %u completed %u preloads\n", targ->myid, (i + 1 - targ->preloadInfo.start));
            fflush(stdout);
        }
    }

    return nullptr;
}

///////////// Actual Workload //////////////////
void* readInsertThread(void* arg) {
    threadarg_t* targ = (threadarg_t*)arg;
    uint32_t iter = 0;

    printf("Thread %u does readCount=%u insertCount=%u\n", targ->myid, targ->readInfo.count, targ->insertInfo.count);
    fflush(stdout);
    while ((targ->readInfo.count > 0) && (targ->insertInfo.count > 0)) {
        if ((rand() % 100) > targ->riRatio) {
            // It is an insert
            --(targ->insertInfo.count);
            TestEntry* te;
            uint32_t ind = targ->insertInfo.start + targ->insertInfo.count;

            Clock::time_point startTime = Clock::now();
            std::unique_ptr<TestEntry> backe;
            bool inserted = targ->hs->insert(std::move(tst.insertEntries[ind]), (fds::HashNode**)&te,
                                             (std::unique_ptr<fds::HashNode>*)&backe);
            targ->insertInfo.time_ns += get_elapsed_time_ns(startTime);
            tst.preloadEntries[ind] = std::move(backe);
            if (inserted) { targ->insertInfo.actual_count++; }
        } else {
            --(targ->readInfo.count);
            uint32_t ind = targ->readInfo.start + targ->readInfo.count;

            // fds::HashKey *k = tst.rawReadEntries[ind]->extract_key();
            Clock::time_point startTime = Clock::now();
#ifdef READ
            TestEntry* te;
            bool isFound = targ->hs->get(*(tst.rawPreloadEntries[ind]->extract_key()), (fds::HashNode**)&te);
            assert(te == tst.rawPreloadEntries[ind]);
            if (isFound) { targ->readInfo.actual_count++; }
#else
            bool isRemoved;
            std::unique_ptr<TestEntry> backe;
            bool isFound = targ->hs->remove(*(tst.rawPreloadEntries[ind]->extract_key()), &isRemoved,
                                            (std::unique_ptr<fds::HashNode>*)&backe);
            tst.preloadEntries[ind] = std::move(backe);
            assert(!isRemoved || tst.preloadEntries[ind].get() == tst.rawPreloadEntries[ind]);
            if (isRemoved) { targ->readInfo.actual_count++; }
#endif
            targ->readInfo.time_ns += get_elapsed_time_ns(startTime);

            assert(isFound);
        }

        if (((++iter) % 1000) == 0) {
            printf("Thread %u completed %u reads/inserts\n", targ->myid, iter);
            fflush(stdout);
        }
    }

    while (targ->readInfo.count > 0) {
        --(targ->readInfo.count);
        uint32_t ind = targ->readInfo.start + targ->readInfo.count;

        Clock::time_point startTime = Clock::now();
#ifdef READ
        TestEntry* te;
        bool isFound = targ->hs->get(*(tst.rawPreloadEntries[ind]->extract_key()), (fds::HashNode**)&te);
        assert(te == tst.rawPreloadEntries[ind]);
        if (isFound) { targ->readInfo.actual_count++; }
#else
        bool isRemoved;
        std::unique_ptr<TestEntry> backe;
        bool isFound = targ->hs->remove(*(tst.rawPreloadEntries[ind]->extract_key()), &isRemoved,
                                        (std::unique_ptr<fds::HashNode>*)&backe);
        tst.preloadEntries[ind] = std::move(backe);
        assert(!isRemoved || tst.preloadEntries[ind].get() == tst.rawPreloadEntries[ind]);
        if (isRemoved) { targ->readInfo.actual_count++; }
#endif
        targ->readInfo.time_ns += get_elapsed_time_ns(startTime);

        assert(isFound);
    }

    while (targ->insertInfo.count > 0) {
        --(targ->insertInfo.count);
        TestEntry* te;
        uint32_t ind = targ->insertInfo.start + targ->insertInfo.count;

        Clock::time_point startTime = Clock::now();
        std::unique_ptr<TestEntry> backe;
        bool inserted = targ->hs->insert(std::move(tst.insertEntries[ind]), (fds::HashNode**)&te,
                                         (std::unique_ptr<fds::HashNode>*)&backe);
        targ->insertInfo.time_ns += get_elapsed_time_ns(startTime);
        tst.preloadEntries[ind] = std::move(backe);

        if (inserted) { targ->insertInfo.actual_count++; }
    }

    return nullptr;
}

/// Utility function
int64_t unformatNum(const char* str) {
    int64_t val;
    char unit = ' ';

    sscanf(str, "%lld%c", &val, &unit);

    switch (unit) {

    case ' ':
        break;

    case 'G':
    case 'g':
        val *= 1000; // fallthru

    case 'M':
    case 'm':
        val *= 1000; // fallthru

    case 'K':
    case 'k':
        val *= 1000;
        break;

    case 'H':
    case 'h':
        val *= 60;

    case 'U':
    case 'u':
        val *= 60;

    case 'S':
    case 's':
        break;
    }

    return val;
}

int main(int argc, char* argv[]) {
    uint32_t nTotalCount = 10000000;
    int nThreads = 4;
    uint32_t nbuckets = 5000;
    uint32_t nUniques = 10000;
    uint32_t readRatio;

    uint32_t i = 0;
    while (++i < argc) {
        if (strcmp(argv[i], "-c") == 0) {
            nTotalCount = (uint32_t)unformatNum(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0) {
            nThreads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0) {
            nbuckets = (uint32_t)unformatNum(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0) {
            readRatio = (uint32_t)unformatNum(argv[++i]);
        } else if (strcmp(argv[i], "-u") == 0) {
            nUniques = (uint32_t)unformatNum(argv[++i]);
        } else {
            cout << "Invalid option " << argv[i] << endl;
            return 1;
        }
    }

    cout << "Testing with nTotalCount=" << nTotalCount << " nThreads=" << nThreads << " nbuckets=" << nbuckets
         << " readRatio=" << readRatio << endl;

    uint32_t nPreloadCount = (nTotalCount * readRatio) / 100;
    uint32_t nReadCount = nPreloadCount;
    uint32_t nInsertCount = nTotalCount - nReadCount;

    initEntries(nPreloadCount, nInsertCount, nUniques);
    fds::HashSet hs(nbuckets);

    cout << "Preloading amount = " << nPreloadCount << " of data first " << endl;
    uint32_t start = 0;
    threadarg_t* targs = new threadarg_t[nThreads];
    for (int i = 0; i < nThreads; i++) {
        targs[i].hs = &hs;
        targs[i].preloadInfo.count = nPreloadCount / nThreads;
        targs[i].preloadInfo.start = start;
        targs[i].myid = i + 1;
        targs[i].riRatio = 0;
        targs[i].preloadInfo.time_ns = 0;
        targs[i].preloadInfo.actual_count = 0;
        targs[i].readInfo.actual_count = 0;
        targs[i].insertInfo.actual_count = 0;
        pthread_create(&targs[i].tid, NULL, preloadThread, (void*)&targs[i]);

        start += targs[i].preloadInfo.count;
    }

    uint64_t total_preload_time_ns = 0;
    uint32_t total_actual_preload = 0;
    for (int i = 0; i < nThreads; i++) {
        pthread_join(targs[i].tid, NULL);
        total_preload_time_ns += targs[i].preloadInfo.time_ns;
        total_actual_preload += targs->preloadInfo.actual_count;
    }

    cout << "Completed " << nPreloadCount << " preloads with actual count=" << total_actual_preload << " in "
         << total_preload_time_ns / nThreads << " nanoseconds" << endl;

    cout << "Starting Read/Insert test with insertCount = " << nInsertCount << " readCount = " << nReadCount << endl;
    uint32_t insert_start = 0;
    uint32_t read_start = 0;
    for (int i = 0; i < nThreads; i++) {
        targs[i].hs = &hs;
        targs[i].readInfo.count = nReadCount / nThreads;
        targs[i].readInfo.start = read_start;
        targs[i].readInfo.time_ns = 0;

        targs[i].insertInfo.count = nInsertCount / nThreads;
        targs[i].insertInfo.start = insert_start;
        targs[i].insertInfo.time_ns = 0;

        targs[i].myid = i + 1;
        targs[i].riRatio = readRatio;

        pthread_create(&targs[i].tid, NULL, readInsertThread, (void*)&targs[i]);
        insert_start += targs[i].insertInfo.count;
        read_start += targs[i].readInfo.count;
    }

    uint64_t total_insert_time_ns = 0;
    uint64_t total_read_time_ns = 0;
    uint64_t total_delete_time_us = 0;

    uint32_t total_actual_inserts = 0;
    uint32_t total_actual_reads = 0;
    for (int i = 0; i < nThreads; i++) {
        pthread_join(targs[i].tid, NULL);

        total_insert_time_ns += targs[i].insertInfo.time_ns;
        total_read_time_ns += targs[i].readInfo.time_ns;
        total_delete_time_us += targs[i].deleteInfo.time_ns;

        total_actual_inserts += targs->insertInfo.actual_count;
        total_actual_reads += targs->readInfo.actual_count;
    }

    cout << "===================================================================" << endl;
    cout << "Completed " << nPreloadCount << " preloads with actual count=" << total_actual_preload << " in "
         << total_preload_time_ns / nThreads << " nanoseconds" << endl;
    cout << "Completed " << nInsertCount << " inserts with actual count=" << total_actual_inserts << " during read in "
         << total_insert_time_ns / nThreads << " nanoseconds" << endl;
    cout << "Completed " << nReadCount << " reads with actual count=" << total_actual_reads << " during inserts in "
         << total_read_time_ns / nThreads << " nanoseconds" << endl;
    cout << "Completed " << nTotalCount << " deletes in " << total_delete_time_us / nThreads << " microseconds" << endl;

    cout << "===================================================================" << endl;
    if (nPreloadCount) {
        double avgSecs = total_preload_time_ns / nThreads / 1000000000000;
        double tps = nPreloadCount / avgSecs;
        cout << "Preload TPS = " << tps << " Avg Latency = " << total_preload_time_ns / nThreads / nPreloadCount
             << " nanoseconds " << endl;
    }

    if (nInsertCount) {
        double avgSecs = total_insert_time_ns / nThreads / 1000000000000;
        double tps = nInsertCount / avgSecs;
        cout << "Insert during read TPS = " << tps
             << " Avg Latency = " << total_insert_time_ns / nThreads / nInsertCount << " nanoseconds " << endl;
    }

    if (nReadCount) {
        double avgSecs = total_read_time_ns / nThreads / 1000000000000;
        double tps = nReadCount / avgSecs;
        cout << "Read during insert TPS = " << tps << " Avg Latency = " << total_read_time_ns / nThreads / nReadCount
             << " nanoseconds " << endl;
    }

#if 0
    avgSecs = total_delete_time_us/nThreads/1000000;
    tps = nTotalCount/avgSecs;
    cout << "Delete TPS = " << tps << endl;
#endif
}
