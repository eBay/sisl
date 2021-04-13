/*
 * malloc_helper.hpp
 *
 *  Created on: 5-May-2020
 *      Author: hkadayam
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <string>

#ifdef __linux__
    #include <malloc.h>
    #include <sys/time.h>
    #include <sys/resource.h>
#endif

#include <sds_logging/logging.h>
#include <nlohmann/json.hpp>

#include "metrics/histogram_buckets.hpp"
#include "metrics/metrics.hpp"

#if defined(JEMALLOC_EXPORT) || defined(USING_JEMALLOC) || defined(USE_JEMALLOC)
    #include <jemalloc/jemalloc.h>
#elif defined(USING_TCMALLOC)
    #include <gperftools/malloc_extension.h>
#endif

namespace sisl {
#ifdef USING_TCMALLOC
class MallocMetrics;
static void get_parse_tcmalloc_stats(nlohmann::json* const j, MallocMetrics* const metrics);
static uint64_t tcmalloc_page_size{8192};
#endif

class MallocMetrics : public MetricsGroupWrapper {
public:
    explicit MallocMetrics() : sisl::MetricsGroupWrapper{"MallocMetrics", "Singelton"} {
        REGISTER_COUNTER(num_times_exceed_soft_threshold, "Number of times mem usage exceeded soft threshold");
        REGISTER_COUNTER(num_times_exceed_aggressive_threshold,
                         "Number of times mem usage exceeded aggressive threshold");

        REGISTER_GAUGE(appln_used_bytes, "Bytes used by the application");
        REGISTER_GAUGE(page_heap_freelist_size, "Bytes in page heap freelist");

#ifdef USING_TCMALLOC
        REGISTER_GAUGE(central_cache_freelist_size, "Bytes in central cache freelist");
        REGISTER_GAUGE(transfer_cache_freelist_size, "Bytes in transfer cache freelist");
#endif
        REGISTER_GAUGE(thread_cache_freelist_size, "Bytes in thread cache freelist");
        REGISTER_GAUGE(os_released_bytes, "Bytes released to OS");

        REGISTER_HISTOGRAM(free_page_span_distribution, "Continuous pages in heap freelist(higher the better)",
                           HistogramBucketsType(LinearUpto128Buckets));
        REGISTER_HISTOGRAM(unmapped_page_span_distribution, "Continuous pages returned back to system",
                           HistogramBucketsType(LinearUpto128Buckets));
        REGISTER_HISTOGRAM(inuse_page_span_distribution, "Continuous pages which are being used by app",
                           HistogramBucketsType(LinearUpto128Buckets));

        register_me_to_farm();
        attach_gather_cb(std::bind(&MallocMetrics::on_gather, this));
    }
    MallocMetrics(const MallocMetrics&) = delete;
    MallocMetrics(MallocMetrics&&) noexcept = delete;
    MallocMetrics& operator=(const MallocMetrics&) = delete;
    MallocMetrics& operator=(MallocMetrics&&) noexcept = delete;

    ~MallocMetrics() { deregister_me_from_farm(); }

    void on_gather() {
#ifdef USING_TCMALLOC
        get_parse_tcmalloc_stats(nullptr, this);
#endif
    }

    static void enable() { get(); }
    static MallocMetrics& get() {
        static MallocMetrics malloc_metrics;
        return malloc_metrics;
    }
};

#if defined(JEMALLOC_EXPORT) || defined(USING_JEMALLOC) || defined(USE_JEMALLOC)
static size_t get_jemalloc_dirty_page_count() {
    const std::string arena_dirty_prefix{"stats.arenas."};
    const std::string arena_dirty_sufix{".pdirty"};
    size_t npages{0};
    unsigned int ua;
    size_t szu{sizeof(ua)};
    if (::mallctl("arenas.narenas", &ua, &szu, nullptr, 0) == 0) {
        for (unsigned int i{0}; i < ua; ++i) {
            const std::string arena_dirty_page_name{arena_dirty_prefix + std::to_string(i) + arena_dirty_sufix};
            size_t arena_dirty_page{0};
            size_t sz{sizeof(arena_dirty_page)};
            if (::mallctl(arena_dirty_page_name.c_str(), &arena_dirty_page, &sz, nullptr, 0) == 0) { npages += arena_dirty_page; }
        }
    } else {
        LOGWARN("fail to get the number of arenas from jemalloc");
    }
    return npages;
}
#endif

/* Get the application total allocated memory. Relies on jemalloc. Returns 0 for other allocator. */
[[maybe_unused]] static size_t get_total_memory(const bool refresh) {
    size_t allocated{0};

#if defined(JEMALLOC_EXPORT) || defined(USING_JEMALLOC) || defined(USE_JEMALLOC)
    size_t sz_allocated{sizeof(allocated)};
    if (refresh) {
        uint64_t out_epoch{0}, in_epoch{1};
        size_t sz_epoch{sizeof(out_epoch)};
        if (::mallctl("epoch", &out_epoch, &sz_epoch, &in_epoch, sz_epoch) != 0) {
            LOGWARN("fail to refresh jemalloc memory usage stats");
        }

        if (::mallctl("stats.allocated", &allocated, &sz_allocated, nullptr, 0) != 0) { allocated = 0; }

        size_t mapped{0};
        size_t sz_mapped{sizeof(mapped)};
        if (::mallctl("stats.mapped", &mapped, &sz_mapped, nullptr, 0) != 0) { mapped = 0; }
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
    else {
        if (::mallctl("stats.allocated", &allocated, &sz_allocated, nullptr, 0) != 0) { allocated = 0; }
    }
#endif
    return allocated;
}

#if defined(JEMALLOC_EXPORT) || defined(USING_JEMALLOC) || defined(USE_JEMALLOC)
static void print_my_jemalloc_data(void* const opaque, const char* const buf) {
    if (opaque && buf) {
        std::string* const json_buf{static_cast< std::string* >(opaque)};
        json_buf->append(buf);
    }
}
#endif

#if defined(USING_TCMALLOC)
static void update_tcmalloc_range_stats(void* const arg, const base::MallocRange* const range) {
    // LOGINFO("Range: address={}, length={}, Type={}, fraction={}", range->address, range->length, range->type,
    //        range->fraction);

    auto& m{MallocMetrics::get()};
    if (range->type == base::MallocRange::Type::FREE) {
        HISTOGRAM_OBSERVE(m, free_page_span_distribution, (range->length / tcmalloc_page_size));
    } else if (range->type == base::MallocRange::Type::UNMAPPED) {
        HISTOGRAM_OBSERVE(m, unmapped_page_span_distribution, (range->length / tcmalloc_page_size));
    } else if (range->type == base::MallocRange::Type::INUSE) {
        HISTOGRAM_OBSERVE(m, inuse_page_span_distribution, (range->length * range->fraction / tcmalloc_page_size));
    }
}

static void get_parse_tcmalloc_stats(nlohmann::json* const j, MallocMetrics* const metrics) {
    const size_t buf_len{static_cast<size_t>(j ? 1024 * 20 : 9999)};
    char* stats_buf{new char[buf_len]};

    MallocExtension::instance()->GetStats(stats_buf, buf_len);
    LOGDEBUG("TCMalloc Detailed stats: {}", stats_buf);

    std::istringstream ss(stats_buf);
    std::string line;

    const std::regex re1{"MALLOC:[\\s=\\+]+(\\d+) (\\(.* MiB\\)) (.*)"};
    const std::regex re2{"MALLOC:[\\s=\\+]+(\\d+)\\s+(.*)"};
    const std::regex re3{"class\\s+\\d+\\s+\\[\\s*(\\d+) bytes \\] :\\s+(\\d+) objs;\\s+(.*) MiB;\\s+(.*) cum MiB"};
    const std::regex re4{"PageHeap:\\s+(\\d+) sizes;\\s+(.*) MiB free;\\s+(.*) MiB unmapped"};
    const std::regex re5{"\\s+(\\d+) pages \\*\\s+(\\d+) spans ~\\s+(.*) MiB;\\s+.* MiB cum; unmapped:\\s+(.*) MiB;"};
    const std::regex re6{"(>\\d+)\\s+large \\*\\s+(\\d+) spans ~\\s+(.*) MiB;\\s+.* MiB cum; unmapped:\\s+(.*) MiB;"};

    while (std::getline(ss, line, '\n')) {
        std::smatch match;
        if (std::regex_search(line, match, re1) && match.size() > 1) {
            if (j) {
                (*j)["Stats"]["Malloc"][match.str(3)] = match.str(1) + match.str(2);
            } else if (match.str(3) == "Bytes in use by application") {
                GAUGE_UPDATE(*metrics, appln_used_bytes, std::stol(match.str(1)));
            } else if (match.str(3) == "Bytes in page heap freelist") {
                GAUGE_UPDATE(*metrics, page_heap_freelist_size, std::stol(match.str(1)));
            } else if (match.str(3) == "Bytes in thread cache freelists") {
                GAUGE_UPDATE(*metrics, thread_cache_freelist_size, std::stol(match.str(1)));
            } else if (match.str(3) == "Bytes in central cache freelist") {
                GAUGE_UPDATE(*metrics, central_cache_freelist_size, std::stol(match.str(1)));
            } else if (match.str(3) == "Bytes in transfer cache freelist") {
                GAUGE_UPDATE(*metrics, transfer_cache_freelist_size, std::stol(match.str(1)));
            } else if (match.str(3) == "Bytes released to OS (aka unmapped)") {
                GAUGE_UPDATE(*metrics, os_released_bytes, std::stol(match.str(1)));
            }
        } else if (std::regex_search(line, match, re2) && match.size() > 1) {
            if (j) (*j)["Stats"]["Malloc"][match.str(2)] = match.str(1);
            if (match.str(2) == "Tcmalloc page size") {
                const auto sz{std::stol(match.str(1))};
                if (sz != 0) { tcmalloc_page_size = sz; }
            }
        } else if (j) {
            if (std::regex_search(line, match, re3) && match.size() > 1) {
                nlohmann::json j1;
                j1["total_objs"] = match.str(2);
                j1["bytes (MiB)"] = match.str(3);
                (*j)["Stats"]["FreeListClasses"][match.str(1)] = j1;
            } else if (std::regex_search(line, match, re4) && match.size() > 1) {
                nlohmann::json j1;
                (*j)["Stats"]["PageHeap"]["total sizes"] = match.str(1);
                (*j)["Stats"]["PageHeap"]["free bytes (MiB)"] = match.str(2);
                (*j)["Stats"]["PageHeap"]["unmapped bytes (MiB)"] = match.str(3);
            } else if ((std::regex_search(line, match, re5) && match.size() > 1) ||
                       (std::regex_search(line, match, re6) && match.size() > 1)) {
                (*j)["Stats"]["PageHeap"]["Page span of"][match.str(1)]["count"] = match.str(2);
                (*j)["Stats"]["PageHeap"]["Page span of"][match.str(1)]["unmapped"] =
                    (std::stol(match.str(3)) * 1048576.0) / tcmalloc_page_size;
            }
        }
    }

    if (metrics) { MallocExtension::instance()->Ranges(nullptr, update_tcmalloc_range_stats); }
    delete [] stats_buf;
}
#endif

[[maybe_unused]] static nlohmann::json get_malloc_stats_detailed() {
    nlohmann::json j;

#if defined(JEMALLOC_EXPORT) || defined(USING_JEMALLOC) || defined(USE_JEMALLOC)
    std::string detailed;
    ::malloc_stats_print(print_my_jemalloc_data, static_cast<void*>(&detailed), "J");

    j["Implementation"] = "JEMalloc";
    j["Stats"] = nlohmann::json::parse(detailed);
#elif defined(USING_TCMALLOC)
    j["Implementation"] = "TCMalloc (possibly)";
    get_parse_tcmalloc_stats(&j, nullptr);
    j["Stats"]["Malloc"]["MemoryReleaseRate"] = MallocExtension::instance()->GetMemoryReleaseRate();
#endif

    char* common_stats;
    size_t common_stats_len;
    FILE* const stream{::open_memstream(&common_stats, &common_stats_len)};
    if (stream != nullptr) {
        ::malloc_info(0, stream);
        std::fclose(stream);

        j["StatsMallocInfo"] = std::string{common_stats, common_stats_len};
        std::free(common_stats);
    }

    return j;
}

[[maybe_unused]] static bool set_memory_release_rate(const double level) {
#if defined(USING_TCMALLOC)
    MallocExtension::instance()->SetMemoryReleaseRate(level);
    return true;
#endif
    return false;
}

[[maybe_unused]] static bool release_mem_if_needed(const size_t soft_threshold, const size_t aggressive_threshold_in) {
    bool ret{false};
#if defined(USING_TCMALLOC)
    size_t mem_usage{0};
    static std::atomic< bool > is_aggressive_decommit{false};
    const size_t aggressive_threshold{std::max(aggressive_threshold_in, soft_threshold)};

    struct rusage usage;
    if ((::getrusage(RUSAGE_SELF, &usage) == 0) && ((static_cast<size_t>(usage.ru_maxrss) * 1024) <= soft_threshold)) {
        mem_usage = static_cast<size_t>(usage.ru_maxrss) * 1024;
    } else {
        // On occassions get_rusage reports elevated value (including vmem). So cross verify with actual usage.
        // The getrusage serves as initial filter to avoid doing ifstream always and also provide a way to run
        // on docker development systems.
        if (auto usage_file{std::ifstream{"/sys/fs/cgroup/memory/memory.usage_in_bytes"}};
            usage_file.is_open()) {
            usage_file >> mem_usage;
        }
    }

    if (mem_usage > aggressive_threshold) {
        LOGINFO("Total memory alloced {} exceed aggressive threshold limit {}, ask tcmalloc to aggressively decommit ",
                mem_usage, aggressive_threshold);
        COUNTER_INCREMENT(MallocMetrics::get(), num_times_exceed_aggressive_threshold, 1);
        MallocExtension::instance()->SetNumericProperty("tcmalloc.aggressive_memory_decommit", 1);
        MallocExtension::instance()->ReleaseFreeMemory();
        is_aggressive_decommit.store(true);
        ret = true;
        goto done;
    }

    if (mem_usage > soft_threshold) {
        LOGINFO("Total memory alloced {} exceed threshold limit {}, ask tcmalloc to release memory", mem_usage,
                soft_threshold);
        COUNTER_INCREMENT(MallocMetrics::get(), num_times_exceed_soft_threshold, 1);
        MallocExtension::instance()->ReleaseFreeMemory();
        ret = true;
    }

    // We recovered from aggressive threshold, set the property back
    if (is_aggressive_decommit.load()) {
        LOGINFO("Total memory alloced {} is restored back to less than aggressive threshold limit {}, ask tcmalloc to "
                "relax aggressively decommit ",
                mem_usage, aggressive_threshold);
        MallocExtension::instance()->SetNumericProperty("tcmalloc.aggressive_memory_decommit", 0);
        is_aggressive_decommit.store(false);
    }
done:
#endif
    return ret;
}
} // namespace sisl