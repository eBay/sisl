//
// Created by Kadayam, Hari on Oct 220 2019
//

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

#ifdef __linux__
    #include <fcntl.h>
#endif

#include <boost/intrusive/slist.hpp>
#include <sds_logging/logging.h>
#include <spdlog/fmt/fmt.h>

#include "memvector.hpp"
#include "obj_allocator.hpp"
#include "stream_tracker.hpp"

using namespace sisl;

THREAD_BUFFER_INIT
SDS_LOGGING_INIT(group_commit)

#pragma pack(1)
struct log_group_header {
    uint32_t magic;
    uint32_t n_log_records;    // Total number of log records
    uint32_t group_size;       // Total size of this group including this header
    uint32_t inline_data_size; // Out of group size how much data is inlined along with log_record_header
    uint32_t prev_grp_checksum;
    uint32_t cur_grp_checksum;
};
#pragma pack()

template < typename charT, typename traits >
std::basic_ostream< charT, traits >& operator<<(std::basic_ostream< charT, traits >& outStream,
                                                const log_group_header& h) {
    // copy the stream formatting
    std::basic_ostringstream< charT, traits > outStringStream;
    outStringStream.copyfmt(outStream);

    // output the date time
    const auto s{fmt::format("magic = {} n_log_records = {} group_size = {} inline_data_size = {} "
                             "prev_grp_checksum = {} cur_grp_checksum = {}",
                             h.magic, h.n_log_records, h.group_size, h.inline_data_size, h.prev_grp_checksum,
                             h.cur_grp_checksum)};
    outStringStream << s;

    // print the stream
    outStream << outStringStream.str();

    return outStream;
}

#pragma pack(1)
struct serialized_log_record {
    int64_t log_idx;
    uint32_t size : 31;
    uint32_t is_inlined : 1;
    uint8_t data[1];
};
#pragma pack()

static constexpr uint32_t dma_boundary{512};

class LogGroup;
struct log_record {
    static constexpr uint32_t inline_size{dma_boundary};

    serialized_log_record* pers_record{nullptr};
    uint8_t* data_ptr;
    uint32_t size;
    void* context;
    LogGroup* log_group; // Group this record is part of

    log_record(uint8_t* const d, const uint32_t sz, void* const ctx) {
        data_ptr = d;
        size = sz;
        context = ctx;
    }

    serialized_log_record* create_serialized(uint8_t* const buf_ptr, const int64_t log_idx) const {
        serialized_log_record* const sr{reinterpret_cast< serialized_log_record* >(buf_ptr)};
        sr->log_idx = log_idx;
        sr->size = size;
        sr->is_inlined = is_inlinebale();
        if (is_inlinebale()) { std::memcpy(static_cast<void*>(sr->data), static_cast<const void*>(data_ptr), size); }
        return sr;
    }

    // NOTE: serialized_log_record contains 1 byte of data already which is why - 1
    size_t inlined_size() const {
        return sizeof(serialized_log_record) + (((size > 0)  && is_inlinebale()) ? size - 1 : 0);
    }
    size_t serialized_size() const { return sizeof(serialized_log_record) + ((size > 0) ? (size - 1) : 0); }
    bool is_inlinebale() const { return (size <= inline_size); }
    static size_t serialized_size(const uint32_t sz) { return sizeof(serialized_log_record) + ((sz > 0 ? (sz - 1) : 0)); }
};

static constexpr uint32_t flush_idx_frequency{64};

struct iovec_wrapper : public iovec {
    iovec_wrapper(void* const base, const size_t len) {
        iov_base = base;
        iov_len = len;
    }
};

class LogGroup {
    template < typename charT, typename traits >
    friend std::basic_ostream< charT, traits >& operator<<(std::basic_ostream< charT, traits >& outStream, const LogGroup& lg);

public:
    static constexpr uint32_t estimated_iovs{10};
    static constexpr size_t inline_log_buf_size{log_record::inline_size * flush_idx_frequency};
    static constexpr size_t max_log_group_size{8192};

    typedef sisl::FlexArray< iovec_wrapper, estimated_iovs > iovec_array;
    friend class LogDev;

    LogGroup() {
        m_cur_log_buf = &m_log_buf[0];
        m_cur_buf_len = inline_log_buf_size;
        m_cur_buf_pos = sizeof(log_group_header);
        m_overflow_log_buf = nullptr;
        m_nrecords = 0;
        m_total_non_inlined_size = 0;

        m_iovecs.emplace_back(static_cast<void*>(m_cur_log_buf), 0);
    }

    void create_overflow_buf(const uint32_t min_needed) {
        const uint32_t new_len{std::max(min_needed, m_cur_buf_len * 2)};
        auto new_buf{std::unique_ptr< uint8_t[] >(new uint8_t[new_len])};
        std::memcpy(static_cast<void*>(new_buf.get()), static_cast<const void*>(m_cur_log_buf), m_cur_buf_len);
        m_overflow_log_buf = std::move(new_buf);
        m_cur_log_buf = m_overflow_log_buf.get();
        m_cur_buf_len = new_len;
        m_iovecs[0].iov_base = static_cast<void*>(m_cur_log_buf);
    }

    bool can_accomodate(const log_record& record) const {
        return ((record.serialized_size() + m_cur_buf_pos + m_total_non_inlined_size) <= max_log_group_size);
    }

    bool add_record(const log_record& record, const int64_t log_idx) {
        if (!can_accomodate(record)) {
            std::cout << "Will exceed max_log_group_size=" << max_log_group_size
                      << " if we add this record for idx=" << log_idx << " Hence stopping adding in this batch";
            return false;
        }

        const auto size{record.inlined_size()};
        if ((m_cur_buf_pos + size) >= m_cur_buf_len) { create_overflow_buf(m_cur_buf_pos + size); }

        // If serialized size is within inline budget and also we have enough room to hold this data, we can copy
        // them, instead of having a iovec element.
        // std::cout << "size to insert=" << size << " inline_size=" << log_record::inline_size
        //          << " cur_buf_pos=" << m_cur_buf_pos << " inline_log_buf_size=" << inline_log_buf_size << "\n";
        record.create_serialized(&m_cur_log_buf[m_cur_buf_pos], log_idx);
        m_cur_buf_pos += size;
        m_iovecs[0].iov_len += size;
        if (!record.is_inlinebale()) {
            // TODO: Round this up to 512 byte boundary
            m_iovecs.emplace_back(static_cast<void*>(record.data_ptr), record.size);
            m_total_non_inlined_size += record.size;
        }
        m_nrecords++;

        return true;
    }

    const iovec_array* finish() {
        log_group_header* const hdr{header()};
        hdr->magic = 0xDABAF00D;
        hdr->n_log_records = m_nrecords;
        hdr->inline_data_size = m_cur_buf_pos;
        hdr->group_size = hdr->inline_data_size + m_total_non_inlined_size;
        hdr->prev_grp_checksum = 0;
        hdr->cur_grp_checksum = 0;

        return &m_iovecs;
    }

    log_group_header* header() const { return reinterpret_cast<log_group_header*>(m_cur_log_buf); }
    iovec_array& iovecs() { return m_iovecs; }

    uint32_t data_size() const { return header()->group_size - sizeof(log_group_header); }

private:
    std::array < uint8_t, inline_log_buf_size> m_log_buf;
    uint8_t* m_cur_log_buf{m_log_buf.data()};
    uint32_t m_cur_buf_len{inline_log_buf_size};
    uint32_t m_cur_buf_pos{sizeof(log_group_header)};

    std::unique_ptr< uint8_t[] > m_overflow_log_buf;

    uint32_t m_nrecords{0};
    uint32_t m_total_non_inlined_size{0};

    // Info about the final data
    iovec_array m_iovecs;
    int64_t m_flush_log_idx_from;
    int64_t m_flush_log_idx_upto;
    uint64_t m_log_dev_offset;
};

template < typename charT, typename traits >
std::basic_ostream< charT, traits >& operator<<(std::basic_ostream< charT, traits >& outStream, const LogGroup& lg) {
    // copy the stream formatting
    std::basic_ostringstream< charT, traits > outStringStream;
    outStringStream.copyfmt(outStream);

    // output the date time
    const auto s {fmt::format("-----------------------------------------------------------------\n"
                             "Header: [{}]\nLog_idx_range:[{} - {}] Offset={} non_inlined_size={}\n"
                             "-----------------------------------------------------------------\n",
                             *(reinterpret_cast<log_group_header*>(lg.m_cur_log_buf)), lg.m_flush_log_idx_from, 
                             lg.m_flush_log_idx_upto, lg.m_log_dev_offset, lg.m_total_non_inlined_size)};
    outStringStream << s;

    // print the stream
    outStream << outStringStream.str();

    return outStream;
}

typedef sisl::ObjectAllocator< LogGroup, 100 > LogGroupAllocator;

class LogDev {
public:
    typedef std::function< void(int64_t, uint64_t, void*) > log_append_cb_t;
    void register_cb(const log_append_cb_t& cb) { m_append_cb = cb; }

    // static constexpr int64_t flush_threshold_size{ 4096 };
    static constexpr int64_t flush_threshold_size{100};
    static constexpr int64_t flush_data_threshold_size{flush_threshold_size - sizeof(log_group_header)};

    int64_t append(uint8_t* const data, const uint32_t size, void* const cb_context) {
        flush_if_needed(size);
        const auto idx{m_log_idx.fetch_add(1, std::memory_order_acq_rel)};
        m_log_records.create(idx, data, size, cb_context);
        return idx;
    }

    void flush_if_needed(const uint32_t record_size) {
        // If after adding the record size, if we have enough to flush, attempt to flush by setting the atomic bool
        // variable.
        const auto actual_size{record_size ? log_record::serialized_size(record_size) : 0};
        const auto pending_sz{m_pending_flush_size.fetch_add(actual_size, std::memory_order_relaxed) + actual_size};
        if (pending_sz >= flush_data_threshold_size) {
            std::cout << "Pending size to flush is " << pending_sz << " greater than flush data threshold "
                      << flush_data_threshold_size << " Flushing now\n";
            bool expected_flushing = false;
            if (m_is_flushing.compare_exchange_strong(expected_flushing, true, std::memory_order_acq_rel)) {
                // We were able to win the flushing competition and now we gather all the flush data and reserve a slot.
                auto lg = prepare_flush();
                m_pending_flush_size.fetch_sub(lg->data_size(), std::memory_order_relaxed);
                std::cout << "After flushing prepared pending size is " << m_pending_flush_size.load() << "\n";
                dummy_do_io(lg->iovecs(), [lg, this](const bool success) { on_flush_completion(lg, success); });
            }
        }
    }

    LogGroup* prepare_flush() {
        int64_t flushing_upto_idx{0};

        auto* lg{LogGroupAllocator::make_object()};
        m_log_records.foreach_active(m_last_flush_idx + 1,
                                     [&](const int64_t idx, const int64_t upto_idx, const log_record& record) -> bool {
                                         if (lg->add_record(record, idx)) {
                                             flushing_upto_idx = upto_idx;
                                             return true;
                                         } else {
                                             return false;
                                         }
                                     });
        lg->finish();
        lg->m_flush_log_idx_from = m_last_flush_idx + 1;
        lg->m_flush_log_idx_upto = flushing_upto_idx;
        lg->m_log_dev_offset = reserve(lg->data_size() + sizeof(log_group_header));

        std::cout << "Flushing upto log_idx = " << flushing_upto_idx << "\n";
        std::cout << "Log Group:\n" << *lg;
        return lg;
    }

    void on_flush_completion(LogGroup* const lg, const bool is_success) {
        assert(is_success);
        m_log_records.complete(lg->m_flush_log_idx_from, lg->m_flush_log_idx_upto);
        m_last_flush_idx = lg->m_flush_log_idx_upto;

        for (auto idx = lg->m_flush_log_idx_from; idx <= lg->m_flush_log_idx_upto; ++idx) {
            auto& record{m_log_records.at(idx)};
            m_append_cb(idx, lg->m_log_dev_offset, record.context);
        }
#if 0
        if (upto_idx > (m_last_truncate_idx + LogDev::truncate_idx_frequency)) {
            std::cout << "Truncating upto log_idx = " << upto_idx << "\n";
            m_log_records.truncate();
        }
#endif
        m_is_flushing.store(false, std::memory_order_release);
        LogGroupAllocator::deallocate(lg);

        // Try to do chain flush if its really needed.
        flush_if_needed(0);
    }

    void dummy_do_io(const LogGroup::iovec_array& iovecs, const std::function< void(bool) >& cb) {
        // LOG INFO("iovecs with {} pieces", iovecs.size());
        for (size_t i{0}; i < iovecs.size(); ++i) {
            std::cout << "Base = " << iovecs[i].iov_base << " Length = " << iovecs[i].iov_len << "\n";
            // LOGINFO("Base = {} Length = {}", iovec.iov_base, iovec.iov_len);
        }
        cb(true);
    }

    uint64_t reserve(const uint32_t size) {
        static uint64_t offset{0};
        auto cur_offset{offset};
        offset += size;
        return cur_offset;
    }

public:
    static constexpr uint32_t truncate_idx_frequency{flush_idx_frequency * 10};

private:
    sisl::StreamTracker< log_record > m_log_records;
    std::atomic< int64_t > m_log_idx{0};
    std::atomic< int64_t > m_pending_flush_size{0};
    std::atomic< bool > m_is_flushing{false};

    // sisl::atomic_status_counter< flush_status_t, flush_status_t::Normal > m_flush_status;
    int64_t m_last_flush_idx{-1};
    int64_t m_last_truncate_idx{-1};
    uint64_t m_offset{0};

    log_append_cb_t m_append_cb;
};

static void on_append_completion(const int64_t idx, const uint64_t offset, void* const ctx) {
    std::cout << "Append completed with log_idx = " << idx << " offset = " << offset << "\n";
}

int main(int argc, char* argv[]) {
    std::array<std::string, 1024> s;
    LogDev ld;
    ld.register_cb(on_append_completion);

    for (size_t i{0}; i < 200; ++i) {
        s[i] = std::to_string(i);
        ld.append(reinterpret_cast<uint8_t*>(const_cast<char*>(s[i].c_str())), s[i].size() + 1, nullptr);
    }
}