//
// Author: Hari Kadayam
// Created on: Apr 7 2021
//
#pragma once

#include <boost/intrusive/slist.hpp>
#include <vector>
#include <string>
#include <folly/Traits.h>
#include <folly/small_vector.h>
#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wattributes"
#endif
#include <folly/SharedMutex.h>
#if defined __clang__ or defined __GNUC__
#pragma GCC diagnostic pop
#endif

#include "fds/buffer.hpp"
#include "hash_entry_base.hpp"
#include "fds/utils.hpp"
#include "utility/enum.hpp"

namespace sisl {

typedef uint8_t small_offset_t;
typedef uint16_t small_count_t;
typedef std::pair< small_offset_t, small_offset_t > small_range_t;

typedef uint32_t big_offset_t;
typedef uint32_t big_count_t;
typedef std::pair< big_offset_t, big_offset_t > big_range_t;

static constexpr big_count_t max_n_per_node = (s_cast< uint64_t >(1) << (sizeof(small_offset_t) * 8));
static constexpr small_offset_t max_offset_in_node = std::numeric_limits< small_offset_t >::max();
static constexpr size_t s_start_seed = 0; // TODO: Pickup a better seed

// static uint32_t range_count(const small_range_t& range) { return range.second - range.first + 1; }

template < typename K >
struct RangeKey {
    K m_base_key;
    big_offset_t m_nth;
    big_count_t m_count;

    RangeKey(const K& k, const big_offset_t nth, const big_count_t count) : m_base_key{k}, m_nth{nth}, m_count{count} {}
    big_offset_t rounded_nth() const { return sisl::round_down(m_nth, max_n_per_node); }
    big_offset_t end_nth() const { return m_nth + m_count - 1; }

    std::size_t compute_hash() const {
        size_t seed = s_start_seed;
        boost::hash_combine(seed, m_base_key);
        boost::hash_combine(seed, m_nth);
        return seed;
    }

    bool operator==(const RangeKey& other) const {
        return ((m_base_key == other.m_base_key) && (m_nth == other.m_nth) && (m_count == other.m_count));
    }

    bool operator<(const RangeKey& other) const {
        if (m_base_key == other.m_base_key) {
            if (m_nth == other.m_nth) {
                return m_count < other.m_count;
            } else {
                return m_nth < other.m_nth;
            }
        } else {
            return m_base_key < other.m_base_key;
        }
    }
};

template < typename K >
class HashBucket;

ENUM(hash_op_t, uint8_t, CREATE, ACCESS, DELETE, RESIZE)

typedef std::function< sisl::byte_view(const sisl::byte_view&, big_offset_t, big_count_t) > value_extractor_cb_t;

class ValueEntryRange;

template < typename K >
class MultiEntryHashNode;

template < typename K >
using key_access_cb_t =
    std::function< void(const ValueEntryBase& base, const RangeKey< K >&, const hash_op_t, int64_t new_size) >;

///////////////////////////////////////////// RangeHashMap Declaration ///////////////////////////////////
template < typename K >
class RangeHashMap {
private:
    static thread_local std::vector< RangeKey< K > > s_kviews;

    uint32_t m_nbuckets;
    HashBucket< K >* m_buckets;
    value_extractor_cb_t m_value_extractor;
    key_access_cb_t< K > m_key_access_cb;

    static thread_local RangeHashMap< K >* s_cur_hash_map;

#ifdef GLOBAL_HASHSET_LOCK
    mutable std::mutex m;
#endif

public:
    RangeHashMap(uint32_t nBuckets, value_extractor_cb_t value_extractor, key_access_cb_t< K > access_cb = nullptr);
    ~RangeHashMap();

    void insert(const RangeKey< K >& key, const sisl::io_blob& value);
    std::vector< std::pair< RangeKey< K >, sisl::byte_view > > get(const RangeKey< K >& input_key);
    void erase(const RangeKey< K >& key);

    static void set_current_instance(RangeHashMap< K >* hmap) { s_cur_hash_map = hmap; }
    static RangeHashMap< K >* get_current_instance() { return s_cur_hash_map; }
    static value_extractor_cb_t& get_value_extractor() { return get_current_instance()->m_value_extractor; }
    static key_access_cb_t< K >& get_access_cb() { return get_current_instance()->m_key_access_cb; }

    template < typename... Args >
    static void call_access_cb(Args&&... args) {
        if (get_current_instance()->m_key_access_cb) {
            (get_current_instance()->m_key_access_cb)(std::forward< Args >(args)...);
        }
    }

    template < typename... Args >
    static sisl::byte_view extract_value(Args&&... args) {
        return (get_current_instance()->m_value_extractor)(std::forward< Args >(args)...);
    }

private:
    HashBucket< K >& get_bucket(const RangeKey< K >& key) const;
    HashBucket< K >& get_bucket(const K& base_key, const big_offset_t nth) const;
    HashBucket< K >& get_bucket(size_t hash_code) const;

    static size_t compute_hash(const K& base_key, const big_offset_t nth) {
        size_t seed = s_start_seed;
        boost::hash_combine(seed, base_key);
        boost::hash_combine(seed, nth);
        return seed;
    }
};

///////////////////////////////////////////// MultiEntryHashNode Definitions ///////////////////////////////////
template < typename K >
class MultiEntryHashNode : public boost::intrusive::slist_base_hook<> {
    friend class HashBucket< K >;
    friend class ValueEntryRange;

private:
    /////////////////////////////////////////////// ValueEntryRange Declaration ///////////////////////////////////
    struct ValueEntryRange : public ValueEntryBase {
        small_range_t m_range;
        sisl::byte_view m_val;

        ValueEntryRange(const small_range_t& range, const sisl::byte_view& val) :
                ValueEntryBase{}, m_range{range}, m_val{val} {}
        ValueEntryRange(const ValueEntryRange&) = default;
        ValueEntryRange& operator=(const ValueEntryRange&) = default;
        ValueEntryRange(ValueEntryRange&&) = default;
        ValueEntryRange& operator=(ValueEntryRange&&) = default;

        int compare_range(const small_offset_t offset) const {
            if ((offset >= m_range.first) && (offset <= m_range.second)) {
                return 0;
            } else if (offset < m_range.first) {
                return -1;
            } else {
                return 1;
            }
        }

        small_count_t count() const { return m_range.second - m_range.first + 1; }
        small_offset_t offset_within(const small_offset_t key_off) const {
            DEBUG_ASSERT_GE(key_off, m_range.first);
            return key_off - m_range.first;
        }

        std::string to_string() const {
            return fmt::format("m_range={}-{} val_size={}", m_range.first, m_range.second, m_val.size());
        }

        void shrink_to_left(const small_offset_t by) {
            m_range.second -= by;
            m_val = RangeHashMap< K >::extract_value(m_val, 0, count());
        }

        void shrink_to_right(const small_offset_t by) {
            m_range.first += by;
            m_val = RangeHashMap< K >::extract_value(m_val, by, count());
        }

        ValueEntryRange extract_left(const MultiEntryHashNode< K >* node, const small_offset_t right_upto) const {
            DEBUG_ASSERT_GE(right_upto, m_range.first);
            const auto new_range = std::make_pair(m_range.first, right_upto);
            auto e =
                ValueEntryRange{new_range, RangeHashMap< K >::extract_value(m_val, 0, offset_within(right_upto) + 1)};
            e.access_cb(node, hash_op_t::CREATE);
            return e;
        }

        ValueEntryRange extract_right(const MultiEntryHashNode< K >* node, const small_offset_t left_from) const {
            DEBUG_ASSERT_LE(left_from, m_range.second);
            const auto new_range = std::make_pair(left_from, m_range.second);
            auto e = ValueEntryRange{
                new_range,
                RangeHashMap< K >::extract_value(m_val, offset_within(left_from), m_range.second - left_from + 1)};
            e.access_cb(node, hash_op_t::CREATE);
            return e;
        }

        void move_left_to(const MultiEntryHashNode< K >* node, const small_offset_t new_right) {
            DEBUG_ASSERT_LE(new_right, m_range.second, "Can't move left with higher offset");
            if (new_right < m_range.second) {
                m_val = RangeHashMap< K >::extract_value(m_val, 0, new_right - m_range.first + 1);
                m_range.second = new_right;
                access_cb(node, hash_op_t::RESIZE);
            }
        }

        void move_right_to(const MultiEntryHashNode< K >* node, const small_offset_t new_left) {
            DEBUG_ASSERT_GE(new_left, m_range.first, "Can't move right with lower offset");
            if (new_left > m_range.first) {
                m_val = RangeHashMap< K >::extract_value(m_val, offset_within(new_left), m_range.second - new_left + 1);
                m_range.first = new_left;
                access_cb(node, hash_op_t::RESIZE);
            }
        }

        void access_cb(const MultiEntryHashNode< K >* node, const hash_op_t op) const {
            RangeHashMap< K >::call_access_cb(*this, node->to_big_key(m_range), op, m_val.size());
        }
    };

    K m_base_key;
    big_offset_t m_base_nth;
    folly::small_vector< ValueEntryRange, 8, small_count_t > m_values;

public:
    MultiEntryHashNode(const K& base_key, big_offset_t nth) : m_base_key{base_key}, m_base_nth{nth} {}

    small_count_t get(const RangeKey< K >& input_key,
                      std::vector< std::pair< RangeKey< K >, sisl::byte_view > >& out_values) const {
        small_count_t count{0};
        small_range_t input_range = to_relative_range(input_key);

        // First binary_search for the location, if there is a valid
        auto [idx, found] = binary_search(-1, int_cast(m_values.size()), input_range.first);
        while (idx < int_cast(m_values.size())) {
            if (input_range.second >= m_values[idx].m_range.first) {
                out_values.emplace_back(extract_matched_kv(m_values[idx], input_range));
                m_values[idx].access_cb(this, hash_op_t::ACCESS);
                LOGDEBUG("Node({}) Getting entry at idx={}, key_range=[{}-{}], val_size={}", to_string(), idx,
                         to_relative_range(out_values.back().first).first,
                         to_relative_range(out_values.back().first).second, out_values.back().second.size());
            } else {
                break;
            }
            input_range.first = m_values[idx].m_range.second + 1;
            ++idx;
            ++count;
        }

        return count;
    }

    void insert(const RangeKey< K >& input_key, sisl::byte_view&& value) {
        const small_range_t input_range = to_relative_range(input_key);

        auto [l_idx, l_found] = binary_search(-1, int_cast(m_values.size()), input_range.first);
        auto [r_idx, r_found] = binary_search(-1, int_cast(m_values.size()), input_range.second);

        bool is_move_to_left = l_found && (input_range.first > m_values[l_idx].m_range.first);
        bool is_move_to_right = r_found && (input_range.second < m_values[r_idx].m_range.second);

        if (l_found && r_found) {
            if (l_idx == r_idx) {
                if (is_move_to_left && is_move_to_right) {
                    // Need to add an additional entry, for shrinking the value of left entry.
                    m_values.insert(m_values.begin() + l_idx,
                                    m_values[l_idx].extract_left(this, input_range.first - 1));
                    LOGDEBUG("Node({}) Splitting entries and added 1 entries at idx={} with first value=[{}]",
                             to_string(), l_idx, m_values[l_idx].to_string());
                    ++l_idx;
                    ++r_idx;
                    is_move_to_left = false;
                }
            }
        }

        if (is_move_to_left) {
            m_values[l_idx].move_left_to(this, input_range.first - 1);
            LOGDEBUG("Node({}) To insert: shrinking entry by moving left at idx={}, new value=[{}]", to_string(), l_idx,
                     m_values[l_idx].to_string());
            ++l_idx;
        }

        if (is_move_to_right) {
            m_values[r_idx].move_right_to(this, input_range.second + 1);
            LOGDEBUG("Node({}) To insert: shrinking entry by moving right at idx={}, new value=[{}]", to_string(),
                     r_idx, m_values[r_idx].to_string());
        } else {
            r_idx = std::min(r_idx + 1, int_cast(m_values.size()));
        }

        // Erase all intermediate entries
        if (r_idx > l_idx) {
            if (RangeHashMap< K >::get_access_cb()) {
                for (auto idx{l_idx}; idx < r_idx; ++idx) {
                    m_values[idx].access_cb(this, hash_op_t::DELETE);
                }
            }
            LOGDEBUG("Node({}) To insert: Erase all entries between idx={} to {} values=[{}] to [{}]", to_string(),
                     l_idx, r_idx - 1, m_values[l_idx].to_string(), m_values[r_idx - 1].to_string());
            m_values.erase(m_values.begin() + l_idx, m_values.begin() + r_idx);
        }

        // Finally insert the entry
        m_values.insert(m_values.begin() + l_idx, ValueEntryRange{input_range, std::move(value)});
        m_values[l_idx].access_cb(this, hash_op_t::CREATE);
        LOGDEBUG("Node({}) To insert: Inserting entry at idx={} value=[{}]", to_string(), l_idx,
                 m_values[l_idx].to_string());
    }

    small_count_t erase(const RangeKey< K >& input_key) {
        const small_range_t input_range = to_relative_range(input_key);
        auto [l_idx, l_found] = binary_search(-1, int_cast(m_values.size()), input_range.first);
        auto [r_idx, r_found] = binary_search(-1, int_cast(m_values.size()), input_range.second);

        bool is_move_to_left = l_found && (input_range.first > m_values[l_idx].m_range.first);
        bool is_move_to_right = r_found && (input_range.second < m_values[r_idx].m_range.second);

        if (l_found && r_found) {
            if (l_idx == r_idx) {
                // The input range is fully inside the current entry
                if (is_move_to_right && is_move_to_left) {
                    // Need to add an additional entry, for shrinking the value of left entry.
                    m_values.insert(m_values.begin() + l_idx,
                                    m_values[l_idx].extract_left(this, input_range.first - 1));
                    LOGDEBUG("Node({}) To erase: Splitting entries and added 1 entries at idx={} with first value=[{}]",
                             to_string(), l_idx, m_values[l_idx].to_string());
                    ++r_idx;
                    ++l_idx;
                    is_move_to_left = false;
                }
            }
        }

        if (is_move_to_left) {
            m_values[l_idx].move_left_to(this, input_range.first - 1);
            LOGDEBUG("Node({}) To erase: shrinking entry by moving left at idx={}, new value=[{}]", to_string(), l_idx,
                     m_values[l_idx].to_string());
            ++l_idx;
        }

        if (is_move_to_right) {
            m_values[r_idx].move_right_to(this, input_range.second + 1);
            LOGDEBUG("Node({}) To erase: shrinking entry by moving right at idx={}, new value=[{}]", to_string(), r_idx,
                     m_values[r_idx].to_string());
        } else {
            r_idx = std::min(r_idx + 1, int_cast(m_values.size()));
        }

        if (r_idx > l_idx) {
            if (RangeHashMap< K >::get_access_cb()) {
                for (auto idx{l_idx}; idx < r_idx; ++idx) {
                    m_values[idx].access_cb(this, hash_op_t::DELETE);
                }
            }
            LOGDEBUG("Node({}) Erase all entries between idx={} to {} values=[{}] to [{}]", to_string(), l_idx,
                     r_idx - 1, m_values[l_idx].to_string(), m_values[r_idx - 1].to_string());
            m_values.erase(m_values.begin() + l_idx, m_values.begin() + r_idx);
        }

        return s_cast< small_count_t >(m_values.size());
    }

    std::string to_string() const { return fmt::format("BaseKey={} Nth_Offset={}", m_base_key, m_base_nth); }

    std::string verbose_to_string() const {
        auto str = fmt::format("BaseKey={} Nth_Offset={} Values=", m_base_key, m_base_nth);
        uint32_t i{0};
        for (auto& v : m_values) {
            fmt::format_to(std::back_inserter(str), "\n[{}]: {}", i++, v.to_string());
        }
        return str;
    }

private:
    std::pair< int, bool > binary_search(int start, int end, const small_offset_t offset) const {
        int mid{0};
        while ((end - start) > 1) {
            mid = start + (end - start) / 2;
            int x = m_values[mid].compare_range(offset);
            if (x == 0) {
                return std::make_pair<>(mid, true);
            } else if (x > 0) {
                start = mid;
            } else {
                end = mid;
            }
        }
        return std::make_pair<>(end, false);
    }

    small_range_t to_relative_range(const RangeKey< K >& input_key) const {
        small_range_t range;
        range.first = input_key.m_nth - m_base_nth;
        range.second = input_key.end_nth() - m_base_nth;
        return range;
    }

    RangeKey< K > to_big_key(const small_range_t range) const {
        return RangeKey< K >{m_base_key, m_base_nth + range.first, uint32_cast(range.second) - range.first + 1};
    }

    std::pair< big_offset_t, big_offset_t > to_big_range(const small_range_t range) const {
        return std::make_pair<>(m_base_nth + range.first, m_base_nth + range.second);
    }

    std::pair< RangeKey< K >, sisl::byte_view > extract_matched_kv(const ValueEntryRange& ventry,
                                                                   const small_range_t& input_range) const {
        small_range_t key_range{std::max(ventry.m_range.first, input_range.first),
                                std::min(ventry.m_range.second, input_range.second)};

        const small_offset_t val_start = ventry.offset_within(key_range.first);
        const small_count_t val_count = ventry.offset_within(key_range.second) - val_start + 1;
        return std::make_pair<>(to_big_key(key_range),
                                RangeHashMap< K >::extract_value(ventry.m_val, val_start, val_count));
    }

    sisl::byte_view extract_matched_value(const ValueEntryRange& ventry, const small_range_t& input_range) const {
        small_range_t key_range{std::max(ventry.m_range.first, input_range.first),
                                std::min(ventry.m_range.second, input_range.second)};
        auto val_start = ventry.offset_within(key_range.first);
        auto val_count = ventry.offset_within(key_range.second) - val_start + 1;
        return RangeHashMap< K >::extract_value(ventry.m_val, val_start, val_count);
    }
};

///////////////////////////////////////////// ValueEntryRange Definitions ///////////////////////////////////

template < typename K >
thread_local sisl::RangeHashMap< K >* sisl::RangeHashMap< K >::s_cur_hash_map{nullptr};

///////////////////////////////////////////// HashBucket Definitions ///////////////////////////////////
template < typename K >
class HashBucket {
private:
#ifndef GLOBAL_HASHSET_LOCK
    mutable folly::SharedMutexWritePriority m_lock;
#endif
    typedef boost::intrusive::slist< MultiEntryHashNode< K > > hash_node_list_t;
    hash_node_list_t m_list;

public:
    HashBucket() = default;

    ~HashBucket() {
        auto it{m_list.begin()};
        while (it != m_list.end()) {
            MultiEntryHashNode< K >* n = &*it;
            it = m_list.erase(it);
            delete n;
        }
    }

    void insert(const RangeKey< K >& input_key, sisl::byte_view&& value) {
#ifndef GLOBAL_HASHSET_LOCK
        folly::SharedMutexWritePriority::WriteHolder holder(m_lock);
#endif
        const auto input_nth_rounded = input_key.rounded_nth();
        MultiEntryHashNode< K >* n = nullptr;
        auto it = m_list.begin();
        for (auto itend{m_list.end()}; it != itend; ++it) {
            if (input_key.m_base_key > it->m_base_key) {
                break;
            } else if (input_key.m_base_key == it->m_base_key) {
                if (input_nth_rounded > it->m_base_nth) {
                    break;
                } else if (input_nth_rounded == it->m_base_nth) {
                    n = &*it;
                    break;
                }
            }
        }

        if (n == nullptr) {
            n = new MultiEntryHashNode< K >(input_key.m_base_key, input_nth_rounded);
            m_list.insert(it, *n);
        }
        n->insert(input_key, std::move(value));
    }

    big_count_t get(const RangeKey< K >& input_key,
                    std::vector< std::pair< RangeKey< K >, sisl::byte_view > >& out_values) {
#ifndef GLOBAL_HASHSET_LOCK
        folly::SharedMutexWritePriority::ReadHolder holder(m_lock);
#endif
        big_count_t ret{0};
        const auto input_nth_rounded = input_key.rounded_nth();

        for (const auto& n : m_list) {
            if (input_key.m_base_key > n.m_base_key) {
                break;
            } else if (input_key.m_base_key == n.m_base_key) {
                if (input_nth_rounded > n.m_base_nth) {
                    break;
                } else if (input_nth_rounded == n.m_base_nth) {
                    ret = n.get(input_key, out_values);
                    break;
                }
            }
        }
        return ret;
    }

    void erase(const RangeKey< K >& input_key) {
#ifndef GLOBAL_HASHSET_LOCK
        folly::SharedMutexWritePriority::WriteHolder holder(m_lock);
#endif
        const auto input_nth_rounded = input_key.rounded_nth();
        MultiEntryHashNode< K >* n = nullptr;
        auto it = m_list.begin();

        for (auto itend{m_list.end()}; it != itend; ++it) {
            if (input_key.m_base_key > it->m_base_key) {
                break;
            } else if (input_key.m_base_key == it->m_base_key) {
                if (input_nth_rounded > it->m_base_nth) {
                    break;
                } else if (input_nth_rounded == it->m_base_nth) {
                    n = &*it;
                    break;
                }
            }
        }

        if (n) {
            const auto node_size = n->erase(input_key);
            // If entire node is erased, free up the node
            if (node_size == 0) {
                m_list.erase(it);
                delete n;
            }
        } else {
            LOGDEBUG("Node(BaseKey={} Nth_Offset={}) NOT found", input_key.m_base_key, input_nth_rounded);
        }
    }

    static int compare(const RangeKey< K >& a, const RangeKey< K >& b) {
        if (a.m_base_key == b.m_base_key) {
            const auto a_nth = a.rounded_nth();
            const auto b_nth = b.rounded_nth();
            if (a_nth == b_nth) { return 0; }
            if (a_nth < b_nth) { return -1; }
            return 1;
        } else if (a.m_base_key < b.m_base_key) {
            return -1;
        }
        return 1;
    }
};

///////////////////////////////////////////// RangeHashMap Definitions ///////////////////////////////////
template < typename K >
RangeHashMap< K >::RangeHashMap(uint32_t nBuckets, value_extractor_cb_t value_extractor,
                                key_access_cb_t< K > access_cb) :
        m_nbuckets{nBuckets}, m_value_extractor{std::move(value_extractor)}, m_key_access_cb{std::move(access_cb)} {
    m_buckets = new HashBucket< K >[nBuckets];
}

template < typename K >
RangeHashMap< K >::~RangeHashMap() {
    delete[] m_buckets;
}

template < typename K >
void RangeHashMap< K >::insert(const RangeKey< K >& input_key, const sisl::io_blob& value) {
#ifdef GLOBAL_HASHSET_LOCK
    std::lock_guard< std::mutex > lk(m);
#endif
    set_current_instance(this);
    auto cur_key_nth = input_key.m_nth;
    auto cur_val_nth = 0;
    auto max_this_node = max_n_per_node - (input_key.m_nth - input_key.rounded_nth());
    RangeKey< K > node_key = input_key; // TODO: Can optimize this by avoiding base_key copy by doing some sort of view
    const sisl::byte_view base_val{value};

    while (cur_key_nth <= input_key.end_nth()) {
        const auto count = std::min(max_this_node, input_key.end_nth() - cur_key_nth + 1);
        node_key.m_nth = cur_key_nth;
        node_key.m_count = count;
        auto& hb = get_bucket(node_key);

        sisl::byte_view node_val = m_value_extractor(base_val, cur_val_nth, count);
        hb.insert(node_key, std::move(node_val));

        cur_key_nth += count;
        cur_val_nth += count;
        max_this_node = max_n_per_node;
    }
}

template < typename K >
std::vector< std::pair< RangeKey< K >, sisl::byte_view > > RangeHashMap< K >::get(const RangeKey< K >& input_key) {
#ifdef GLOBAL_HASHSET_LOCK
    std::lock_guard< std::mutex > lk(m);
#endif
    set_current_instance(this);

    std::vector< std::pair< RangeKey< K >, sisl::byte_view > > out_vals;
    auto cur_key_nth = input_key.m_nth;
    auto cur_val_nth = 0;
    auto max_this_node = max_n_per_node - (input_key.m_nth - input_key.rounded_nth());
    RangeKey< K > node_key = input_key; // TODO: Can optimize this by avoiding base_key copy by doing some sort of view

    while (cur_key_nth <= input_key.end_nth()) {
        const auto count = std::min(max_this_node, input_key.end_nth() - cur_key_nth + 1);
        node_key.m_nth = cur_key_nth;
        node_key.m_count = count;

        auto& hb = get_bucket(node_key);
        hb.get(node_key, out_vals);

        cur_key_nth += count;
        cur_val_nth += count;
        max_this_node = max_n_per_node;
    }
    return out_vals;
}

template < typename K >
void RangeHashMap< K >::erase(const RangeKey< K >& input_key) {
#ifdef GLOBAL_HASHSET_LOCK
    std::lock_guard< std::mutex > lk(m);
#endif
    set_current_instance(this);
    auto cur_key_nth = input_key.m_nth;
    auto max_this_node = max_n_per_node - (input_key.m_nth - input_key.rounded_nth());
    RangeKey< K > node_key = input_key; // TODO: Can optimize this by avoiding base_key copy by doing some sort of view

    while (cur_key_nth <= input_key.end_nth()) {
        const auto count = std::min(max_this_node, input_key.end_nth() - cur_key_nth + 1);
        node_key.m_nth = cur_key_nth;
        node_key.m_count = count;
        auto& hb = get_bucket(node_key);
        hb.erase(node_key);
        cur_key_nth += count;
        max_this_node = max_n_per_node;
    }
}

template < typename K >
HashBucket< K >& RangeHashMap< K >::get_bucket(const RangeKey< K >& key) const {
    return (m_buckets[compute_hash(key.m_base_key, key.rounded_nth()) % m_nbuckets]);
}

template < typename K >
HashBucket< K >& RangeHashMap< K >::get_bucket(const K& base_key, const big_offset_t nth) const {
    return (m_buckets[compute_hash(base_key, nth) % m_nbuckets]);
}

template < typename K >
HashBucket< K >& RangeHashMap< K >::get_bucket(size_t hash_code) const {
    return (m_buckets[hash_code % m_nbuckets]);
}

} // namespace sisl
