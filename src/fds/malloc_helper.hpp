/*
 * malloc_helper.hpp
 *
 *  Created on: 5-May-2020
 *      Author: hkadayam
 */

#pragma once

#include <sds_logging/logging.h>
#include <nlohmann/json.hpp>
#include <malloc.h>
#include <stdlib.h>
#include <cstdio>
#include <regex>
#include <string>

#if defined(JEMALLOC_EXPORT) || defined(USING_JEMALLOC)
#include <jemalloc/jemalloc.h>
#elif defined(USING_TCMALLOC)
#include <gperftools/malloc_extension.h>
#endif

namespace sisl {
#if defined(JEMALLOC_EXPORT) || defined(USING_JEMALLOC)
static size_t get_jemalloc_dirty_page_count() {
    const char* arena_dirty_prefix = "stats.arenas.";
    const char* arena_dirty_sufix = ".pdirty";
    size_t npages = 0;
    size_t szu = sizeof(unsigned int);
    unsigned int ua;
    if (mallctl("arenas.narenas", &ua, &szu, NULL, 0) == 0) {
        for (unsigned int i = 0; i < ua; i++) {
            char arena_index[11];
            sprintf(arena_index, "%d", i);
            size_t index_length = strlen(arena_index);
            char arena_dirty_page_name[21 + index_length];
            memcpy(arena_dirty_page_name, arena_dirty_prefix, 13);
            memcpy(arena_dirty_page_name + 13, arena_index, index_length);
            memcpy(arena_dirty_page_name + 13 + index_length, arena_dirty_sufix, 8);

            size_t sz = sizeof(size_t);
            size_t arena_dirty_page = 0;
            if (mallctl(arena_dirty_page_name, &arena_dirty_page, &sz, NULL, 0) == 0) { npages += arena_dirty_page; }
        }
    } else {
        LOGWARN("fail to get the number of arenas from jemalloc");
    }
    return npages;
}
#endif

/* Get the application total allocated memory. Relies on jemalloc. Returns 0 for other allocator. */
[[maybe_unused]] static size_t get_total_memory(bool refresh) {
    size_t allocated = 0;

#if defined(JEMALLOC_EXPORT) || defined(USING_JEMALLOC)
    size_t sz_allocated = sizeof(allocated);
    if (refresh) {
        uint64_t epoch = 1;
        size_t sz_epoch = sizeof(epoch);
        if (mallctl("epoch", &epoch, &sz_epoch, &epoch, sz_epoch) != 0) {
            LOGWARN("fail to refresh jemalloc memory usage stats");
        }

        if (mallctl("stats.allocated", &allocated, &sz_allocated, NULL, 0) != 0) { allocated = 0; }

        size_t mapped = 0;
        if (mallctl("stats.mapped", &mapped, &sz_allocated, NULL, 0) != 0) { mapped = 0; }
        LOGINFO("Allocated memory: {} mapped: {} Dirty page count: {}", allocated, mapped,
                get_jemalloc_dirty_page_count());

        /*
        // enable back ground thread to recycle memory hold by idle threads. It impacts performance. Enable it only if
        //  dirty page count is too high.
        bool set_background_thread = true;
        size_t sz_background_thread = sizeof(set_background_thread);
        if (mallctl("background_thread", NULL, NULL, &set_background_thread, sz_background_thread) != 0) {
            LOGWARN("fail to enable back ground thread for jemalloc";
        } */
    }
    if (mallctl("stats.allocated", &allocated, &sz_allocated, NULL, 0) != 0) { allocated = 0; }
#endif
    return allocated;
}

#if defined(JEMALLOC_EXPORT) || defined(USING_JEMALLOC)
static void print_my_jemalloc_data(void* opaque, const char* buf) {
    std::string* json_buf = (std::string*)opaque;
    *json_buf += buf;
}
#endif

static nlohmann::json get_malloc_stats_detailed() {
    nlohmann::json j;

#if defined(JEMALLOC_EXPORT) || defined(USING_JEMALLOC)
    std::string detailed;
    malloc_stats_print(print_my_jemalloc_data, (void*)&detailed, "J");

    j["Implementation"] = "JEMalloc";
    j["Stats"] = nlohmann::json::parse(detailed);
#elif defined(USING_TCMALLOC)
    char _detailed[1024 * 20];
    MallocExtension::instance()->GetStats(_detailed, 1024 * 20);

    j["Implementation"] = "TCMalloc (possibly)";
    LOGDEBUG("TCMalloc Detailed stats: {}", _detailed);

    std::stringstream ss(_detailed);
    std::string line;
    if (_detailed != NULL) {
        std::regex re1("MALLOC:[\\s=\\+]+(\\d+) (\\(.* MiB\\)) (.*)");
        std::regex re2("MALLOC:[\\s=\\+]+(\\d+)\\s+(.*)");
        std::regex re3("class\\s+\\d+\\s+\\[\\s*(.*)\\] :\\s+(\\d+) objs;\\s+(.*) MiB;\\s+(.*) cum MiB");
        std::regex re4("PageHeap:\\s+(\\d+) sizes;\\s+(.*) MiB free;\\s+(.*) MiB unmapped");

        while (std::getline(ss, line, '\n')) {
            std::smatch match;
            if (std::regex_search(line, match, re1) && match.size() > 1) {
                j["Stats"]["Malloc"][match.str(3)] = match.str(1) + match.str(2);
            } else if (std::regex_search(line, match, re2) && match.size() > 1) {
                j["Stats"]["Malloc"][match.str(2)] = match.str(1);
            } else if (std::regex_search(line, match, re3) && match.size() > 1) {
                nlohmann::json j1;
                j1["total_objs"] = match.str(2);
                j1["bytes (MiB)"] = match.str(3);
                j1["cumulative_bytes (MiB)"] = match.str(4);
                j["Stats"]["FreeListClasses"][match.str(1)] = j1;
            } else if (std::regex_search(line, match, re4) && match.size() > 1) {
                nlohmann::json j1;
                j["Stats"]["PageHeap"]["total sizes"] = match.str(1);
                j["Stats"]["PageHeap"]["free bytes (MiB)"] = match.str(2);
                j["Stats"]["PageHeap"]["unmapped bytes (MiB)"] = match.str(34);
            }
        }
    }
#endif

    char* common_stats;
    size_t common_stats_len;
    FILE* stream = open_memstream(&common_stats, &common_stats_len);
    if (stream != NULL) {
        malloc_info(0, stream);
        fclose(stream);

        j["StatsMallocInfo"] = std::string(common_stats, common_stats_len);
        free(common_stats);
    }

    return j;
}
} // namespace sisl