//
// Created by Kadayam, Hari on Oct 220 2019
//
#include "stream_tracker.hpp"
#include "memvector.hpp"
#include <fcntl.h>
#include <sds_logging/logging.h>

using namespace sisl;
THREAD_BUFFER_INIT;
SDS_LOGGING_INIT(group_commit)

struct log_group_header {
    uint32_t n_log_records;
    uint32_t log_group_size;
    uint32_t prev_grp_checksum;
};

struct log_group_footer {
    uint32_t cur_grp_checksum;
};

struct log_record {
    struct serialized_log_record {
        uint64_t log_idx;
        uint32_t size;
        uint8_t data[0];
    } __attribute__((packed));

    serialized_log_record pers_record;
    uint64_t offset;
    uint8_t* data_ptr;

    log_record(uint8_t* d, uint32_t sz) {
        data_ptr = d;
        pers_record.size = sz;
    }

    void set_idx(int64_t idx) { pers_record.log_idx = idx; }
    size_t data_size() const { return pers_record.size; }
    size_t serialized_size() const { return sizeof(serialized_log_record) + data_size(); }
};

static constexpr uint32_t flush_idx_frequency = 64;

struct iovec_wrapper : public iovec {
    iovec_wrapper(void* base, size_t len) {
        iov_base = base;
        iov_len = len;
    }
};

class LogGroup {
public:
    static constexpr uint32_t estimated_iovs = 128;
    static constexpr uint32_t inline_size = 128;
    static constexpr size_t inline_log_buf_size = inline_size * flush_idx_frequency;

    void prepare() {
        m_iovecs.reserve(estimated_iovs);
        reset();
        m_iovecs.emplace_back((void*)&m_header, sizeof(m_header));
        m_iovecs.emplace_back((void*)&m_log_buf, 0);
    }

    void add_record(const log_record& record) {
        auto size = record.serialized_size();

        // If serialized size is within inline budget and also we have enough room to hold this data, we can copy
        // them, instead of having a iovec element.
        std::cout << "size to insert=" << size << " inline_size=" << inline_size << " cur_buf_pos=" << m_cur_buf_pos
                  << " inline_log_buf_size=" << inline_log_buf_size << "\n";
        if ((size < inline_size) && ((m_cur_buf_pos + size) < inline_log_buf_size)) {
            uint8_t* buf = &m_log_buf[m_cur_buf_pos];
            memcpy(buf, record.data_ptr, record.data_size());
            m_cur_buf_pos += size;
            m_iovecs[m_iovecs.size() - 1].iov_len += size;
        } else {
            m_iovecs.emplace_back((void*)&(record.pers_record), sizeof(log_record::serialized_log_record));
            m_iovecs.emplace_back((void*)record.data_ptr, record.data_size());
            m_iovecs.emplace_back((void*)&m_log_buf[m_cur_buf_pos], 0);
        }
    }

    const std::vector< iovec_wrapper >& finish() {
        m_iovecs.emplace_back((void*)&m_footer, sizeof(log_group_footer));
        return m_iovecs;
    }

    void reset() {
        bzero(&m_header, sizeof(log_group_header));
        bzero(&m_footer, sizeof(log_group_footer));
        m_cur_buf_pos = 0;
        m_iovecs.clear();
    }

    log_group_header& header() { return m_header; }
    log_group_footer& footer() { return m_footer; }

private:
    log_group_header m_header;
    log_group_footer m_footer;
    uint8_t m_log_buf[inline_log_buf_size];
    uint32_t m_cur_buf_pos = 0;
    std::vector< iovec_wrapper > m_iovecs;
};

class LogDev {
public:
    int64_t append(uint8_t* data, uint32_t size) {
        auto idx = m_log_idx.fetch_add(1, std::memory_order_acq_rel);
        m_log_records.set(idx, data, size);
        if (idx >= (m_last_flush_idx + flush_idx_frequency)) {
            bool expect_flushing = false;
            if (m_is_flushing.compare_exchange_strong(expect_flushing, true)) { flush(); }
        }
        return idx;
    }

    void flush() {
        uint32_t nrecords = 0u;
        int64_t flushing_upto_idx = 0u;

        lg.prepare();
        size_t start_offset = m_offset;
        m_offset += sizeof(log_group_header); // Leave header room
        m_log_records.foreach_completed(m_last_flush_idx + 1,
                                        [&](int64_t idx, int64_t upto_idx, log_record& record) -> bool {
                                            flushing_upto_idx = upto_idx;

                                            // Adjust the idx and offset for each writes
                                            record.set_idx(idx);
                                            record.offset = m_offset;
                                            lg.add_record(record);

                                            nrecords++;
                                            m_offset += record.serialized_size();
                                            return true;
                                        });
        m_offset += sizeof(log_group_footer);
        auto& iovecs = lg.finish();

        lg.header().n_log_records = nrecords;
        lg.header().log_group_size = m_offset - start_offset;
        lg.header().prev_grp_checksum = 0;
        lg.footer().cur_grp_checksum = 0;

        std::cout << "Flushing upto log_idx = " << flushing_upto_idx << "\n";
        dummy_do_io(iovecs,
                    [flushing_upto_idx, this](bool success) { on_flush_completion(flushing_upto_idx, success); });
    }

    void on_flush_completion(int64_t upto_idx, bool is_success) {
        m_last_flush_idx = upto_idx;
        if (upto_idx > (m_last_truncate_idx + LogDev::truncate_idx_frequency)) {
            std::cout << "Truncating upto log_idx = " << upto_idx << "\n";
            m_log_records.truncate();
        }
        std::cout << "Flushing completed \n";
        m_is_flushing.store(false);
    }

    void dummy_do_io(const std::vector< iovec_wrapper >& iovecs, const std::function< void(bool) >& cb) {
        // LOG INFO("iovecs with {} pieces", iovecs.size());
        for (auto& iovec : iovecs) {
            std::cout << "Base = " << iovec.iov_base << " Length = " << iovec.iov_len << "\n";
            // LOGINFO("Base = {} Length = {}", iovec.iov_base, iovec.iov_len);
        }
        cb(true);
    }

public:
    static constexpr uint32_t truncate_idx_frequency = flush_idx_frequency * 10;

private:
    static LogGroup lg;

    sisl::StreamTracker< log_record > m_log_records;
    std::atomic< int64_t > m_log_idx = 0;
    std::atomic< bool > m_is_flushing = false;
    int64_t m_last_flush_idx = -1;
    int64_t m_last_truncate_idx = -1;
    uint64_t m_offset = 0;
};

// thread_local uint8_t LogGroup::_log_buf[inline_log_buf_size];
// thread_local std::vector< iovec_wrapper > LogGroup::_iovecs;

LogGroup LogDev::lg;

int main(int argc, char* argv[]) {
    std::string s[1024];
    LogDev ld;
    for (auto i = 0u; i < 200; i++) {
        s[i] = std::to_string(i);
        ld.append((uint8_t*)s[i].c_str(), s[i].size() + 1);
    }
}