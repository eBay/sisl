<<<<<<< Updated upstream
=======
//
// Author: Hari Kadayam
// Created on: Apr 7 2021
//
>>>>>>> Stashed changes
#pragma once

#include <boost/intrusive/slist.hpp>
#include <vector>
#include <string>
#include <folly/Traits.h>
#include <fds/utils.hpp>
#include <utility/enum.hpp>
<<<<<<< Updated upstream

=======
#include <buffer.hpp>
#include <folly/small_vector.h>

#if 0
>>>>>>> Stashed changes
#define DECLARE_RELOCATABLE(T)                                                                                         \
    namespace folly {                                                                                                  \
    template <>                                                                                                        \
    FOLLY_ASSUME_RELOCATABLE(T);                                                                                       \
    }

<<<<<<< Updated upstream
//////////////////////////////////// SFINAE Hash Selection /////////////////////////////////
namespace {
template < typename T, typename = std::void_t<> >
struct is_std_hashable : std::false_type {};

template < typename T >
struct is_std_hashable< T, std::void_t< decltype(std::declval< std::hash< T > >()(std::declval< T >())) > >
        : std::true_type {};

template < typename T >
constexpr bool is_std_hashable_v{is_std_hashable< T >::value};

template < typename K >
uint64_t compute_hash_imp(const K& key, std::true_type) {
    return static_cast< uint64_t >(std::hash< K >()(key));
}

template < typename K >
uint64_t compute_hash_imp(const K& key, std::false_type) {
    const auto b{K::get_blob(key)};
    const uint64_t hash_code{util::Hash64(reinterpret_cast< const char* >(b.bytes), static_cast< size_t >(b.size))};
    return hash_code;
}

// range by data helper templates that does tag dispatching based on multivalued
template < typename K >
uint64_t compute_hash(const K& key) {
    return compute_hash_imp< K >(key, is_std_hashable< K >{});
}
} // namespace

namespace sisl {

typedef uint8_t koffset_t;
typedef uint16_t kcount_t;
typedef std::pair< koffset_t, koffset_t > koffset_range_t;

ENUM(match_type_t, uint8_t, no_match_pre, no_match_post, exact, superset, subset, pre_partial, post_partial)

template < typename K >
struct KeyView {
    K m_base_key;
    uint32_t m_size;
    koffset_t m_offset;

    KeyView(const K& base, const koffset_t o, const uint32_t s) : m_base_key{base}, m_offset{o}, m_size{s} {}

    koffset_range_t range() const { return std::make_pair<>(m_offset, m_offset + m_size - 1); }
};

template < typename K, typename K >
struct ValueView {
    KeyView m_kview;
    V m_val;
};

template < typename K, typename V >
class MultiEntryHashNode : public boost::intrusive::slist_base_hook<> {
    static_assert(
        folly::IsRelocatable< V >::value == 1,
        "Expect Value type to be relocatable, if it is indeed relocatable, define it as DECLARE_RELOCATABLE(type)");

    static constexpr uint32_t min_alloc_nentries = 4u;
    static constexpr uint32_t max_alloc_nentries = (1u << (sizeof(koffset_t) * 8));

public:
    struct val_entry_info {
        koffset_range_t range;
        bool is_valid{false};
        uint8_t val_buf[1];

        int compare_range(const koffset_t offset) const {
            if ((offset >= range.first) && (offset <= range.second)) {
                return 0;
            } else if (offset < range.first) {
                return -1;
            } else {
                return 1;
            }
        }

        std::string to_string() const {
            return fmt::format("offset={}-{} is_valid={} value={}", range.first, range.second, is_valid,
                               V::to_string(*get_value_const()));
        }

        static size_t size() { return sizeof(val_entry_info) - sizeof(uint8_t) + sizeof(V); }
        V* get_value() { return r_cast< V* >(&val_buf[0]); }
        const V* get_value_const() const { return r_cast< const V* >(&val_buf[0]); }
        uint8_t* get_raw_value() { return (&val_buf[0]); }
        inline koffset_t low() const { return range.first; }
        inline koffset_t high() const { return range.second; }
    };

public:
    static MultiEntryHashNode< K, V >* alloc_node(const K& key, const uint32_t nentries = min_alloc_nentries) {
        assert((nentries > 0) && (nentries <= max_alloc_nentries));
        uint8_t* buf{new uint8_t[size(nentries)]};
        auto n = new (buf) MultiEntryHashNode< K, V >();
        n->m_key = key;
        n->m_max_alloc_idx = s_cast< koffset_t >(nentries - 1);
        return n;
    }

    static std::pair< MultiEntryHashNode< K, V >*, bool > resize_if_needed(MultiEntryHashNode< K, V >* cur_node,
                                                                           const koffset_t addln_entries) {
        if (cur_node->num_entries() == max_alloc_nentries) { return std::make_pair<>(cur_node, false); }

        const uint32_t needed_nentries{std::max((cur_node->num_entries() + addln_entries), min_alloc_nentries)};
        assert(needed_nentries < cur_node->alloced_entries() * 2u);

        if (needed_nentries > cur_node->alloced_entries()) {
            return std::make_pair<>(
                realloc_node(cur_node, std::max(needed_nentries, (cur_node->alloced_entries() * 2u))), true);
        } else if (cur_node->alloced_entries() > (needed_nentries * 2)) {
            // Need to shrink if we have double
            // cur_node->compact();
            return std::make_pair<>(realloc_node(cur_node, needed_nentries * 2), true);
        } else {
            return std::make_pair<>(cur_node, false);
        }
    }

    static MultiEntryHashNode< K, V >* realloc_node(MultiEntryHashNode< K, V >* cur_node, const uint32_t new_nentries) {
        const auto cur_size{size(cur_node->num_entries())};
        const auto new_alloc_size{size(new_nentries)};
        assert(new_alloc_size > cur_size);

        uint8_t* buf{new uint8_t[new_alloc_size]};
        memcpy(buf, r_cast< uint8_t* >(cur_node), cur_size);

        LOGDEBUG("Realloc node={} with cur_entries={} cur_size={} cur_alloc_entries={} to new_node={} new_size={} "
                 "new_alloc_entries={}",
                 r_cast< void* >(cur_node), cur_node->num_entries(), cur_size, cur_node->alloced_entries(),
                 r_cast< void* >(buf), new_alloc_size, new_nentries);
        delete cur_node;

        auto n = r_cast< MultiEntryHashNode< K, V >* >(buf);
        n->m_max_alloc_idx = s_cast< koffset_t >(new_nentries - 1);
        return n;
    }

    uint32_t find(const koffset_range_t range, std::vector< const val_entry_info* >& out_values) const {
        uint32_t count{0};
        // First binary_search for the location, if there is a valid
        auto [idx, found] = binary_search(-1, s_cast< int >(num_entries()), range.first);
        while (found) {
            if (get_nth_const(idx)->is_valid) {
                out_values.push_back(get_nth_const(idx));
                ++count;
            }
            if (idx == m_max_entry_idx) break;

            // Check if upper bound of the input_range is within bounds of next entry.
            found = (get_nth_const(++idx)->compare_range(range.second) >= 0);
        }
        return count;
    }

    uint32_t get(const koffset_range_t range, std::vector< ValueView< K, V > >& out_values) const {
        uint32_t count{0};
        // First binary_search for the location, if there is a valid
        auto [idx, found] = binary_search(-1, s_cast< int >(num_entries()), range.first);
        while (found) {
            if (get_nth_const(idx)->is_valid) {
                out_values.push_back(to_value_view(get_nth_const(idx)));
                ++count;
            }
            if (idx == m_max_entry_idx) break;

            // Check if upper bound of the input_range is within bounds of next entry.
            found = (get_nth_const(++idx)->compare_range(range.second) >= 0);
=======
#endif

namespace sisl {

typedef uint8_t small_offset_t;
typedef uint16_t small_count_t;
typedef std::pair< small_offset_t, small_offset_t > small_range_t;

typedef uint32_t big_offset_t;
typedef uint32_t big_count_t;
typedef std::pair< big_offset_t, big_offset_t > big_range_t;

static constexpr big_count_t max_n_per_node = (static_cast< uint64_t >(1) << (sizeof(small_offset_t) * 8));

template < typename K >
struct RangeKey {
    K m_base_key;
    big_offset_t m_nth;
    big_count_t m_count;

    RangeKey(const K& k, const big_offset_t nth, const big_count_t count) : m_base_key{k}, m_nth{nth}, m_count{count} {}
    big_offset_t rounded_nth() const { return sisl::round_down(m_nth, max_n_per_node); }
    big_offset_t end_nth() const { return m_nth + m_count - 1; }
};

template < typename K >
class HashBucket;

ENUM(hash_op_t, uint8_t, CREATE, ACCESS, DELETE)

typedef std::function< sisl::byte_view(const sisl::byte_view&, big_offset_t, big_count_t) > value_extractor_cb_t;

template < typename K >
using key_access_cb_t = std::function< void(const RangeKey< K >&, const hash_op_t, void*) >;

template < typename K >
class RangeHashMap {
private:
    static thread_local std::vector< RangeKey< K > > s_kviews;
    static constexpr size_t s_start_seed = 0; // TODO: Pickup a better seed

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

    big_count_t insert(const RangeKey< K >& key, const sisl::io_blob& value);
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

template < typename K >
class MultiEntryHashNode : public boost::intrusive::slist_base_hook<> {
    friend class HashBucket< K >;

private:
    struct value_info {
        small_range_t m_range;
        sisl::byte_view m_val;
        void* m_context;

        int compare_range(const small_offset_t offset) const {
            if ((offset >= m_range.first) && (offset <= m_range.second)) {
                return 0;
            } else if (offset < m_range.first) {
                return -1;
            } else {
                return 1;
            }
        }

        value_info(const small_range_t& range, const sisl::byte_view& val, void* context = nullptr) :
                m_range{range}, m_val{val}, m_context{context} {}

        small_count_t count() const { return m_range.second - m_range.first + 1; }
        small_offset_t offset_within(const small_offset_t off) const { return off - m_range.first; }
    };

    K m_base_key;
    big_offset_t m_base_nth;
    folly::small_vector< value_info, 8, small_count_t > m_values;

public:
    MultiEntryHashNode(const K& base_key, big_offset_t nth) : m_base_key{base_key}, m_base_nth{nth} {}

    big_count_t get(const RangeKey< K >& input_key,
                    std::vector< std::pair< RangeKey< K >, sisl::byte_view > >& out_values) const {
        big_count_t count{0};
        const small_range_t input_range = to_relative_range(input_key);

        // First binary_search for the location, if there is a valid
        auto [idx, found] = binary_search(-1, static_cast< int >(m_values.size()), input_range.first);
        while (found) {
            out_values.emplace_back(extract_matched_kv(m_values[idx], input_range));
            RangeHashMap< K >::call_access_cb(out_values.back().first, hash_op_t::ACCESS, m_values[idx].m_context);
            ++count;

            if (++idx == static_cast< int >(m_values.size())) break;

            // Check if upper bound of the input_range is within bounds of next entry.
            found = (m_values[idx].compare_range(input_range.second) >= 0);
>>>>>>> Stashed changes
        }
        return count;
    }

<<<<<<< Updated upstream
    template < class... Args >
    std::pair< V*, bool > try_emplace(const koffset_range_t range, Args&&... args) {
        int shift_count{0};

        auto [start_idx, found] = binary_search(-1, s_cast< int >(num_entries()), range.first);
        int end_idx{start_idx};

        if (start_idx > m_max_entry_idx) {
            ++m_max_entry_idx;
            goto insert; // Entry out of the existing bound, directly insert at idx.
        } else if (!found && (range.second < get_nth_const(start_idx)->range.first)) {
            shift_count = 1; // New entry in the middle, need to shift right to make room.
        } else {
            if (get_nth(start_idx)->is_valid) { return std::make_pair<>(get_nth(start_idx)->get_value(), false); }
            while (++end_idx <= m_max_entry_idx) {
                auto m = get_match_type(get_nth_const(end_idx)->range, range);
                if ((m == match_type_t::no_match_pre) || (m == match_type_t::no_match_post)) {
                    break;
                } else {
                    assert((m == match_type_t::exact) || (m == match_type_t::superset) ||
                           (m == match_type_t::pre_partial));
                    if (get_nth(end_idx)->is_valid) { return std::make_pair<>(get_nth(end_idx)->get_value(), false); }
                    --shift_count;
                }
            }
        }

        if (shift_count < 0) {
            shift_left(end_idx, -shift_count);
        } else if (shift_count > 0) {
            assert(shift_count == 1);
            shift_right(start_idx, shift_count);
        }

    insert:
        auto vinfo = get_nth(start_idx);
        LOGTRACE("Inserting in idx={} addr={}", start_idx, r_cast< void* >(vinfo->get_raw_value()));
        V* val = new (vinfo->get_raw_value()) V(std::forward< Args >(args)...);
        vinfo->is_valid = true;
        vinfo->range = range;
        m_valid_count += range.second - range.first + 1;
        return std::make_pair<>(val, true);
    }

    uint32_t num_entries_in_range(koffset_range_t range) const {
        const auto s{binary_search(-1, s_cast< int >(num_entries()), range.first)};
        const auto e{binary_search(-1, s_cast< int >(num_entries()), range.first)};
        return e.first - s.first + 1;
    }

    template < class... Args >
    void emplace_or_update(koffset_range_t range, const auto& update_cb, Args&&... args) {
        int shift_count{0};
        auto [idx, found] = binary_search(-1, s_cast< int >(num_entries()), range.first);

        // Check if entire range is not found, emplace one entry and done
        if ((idx > m_max_entry_idx) || (!found && (get_nth_const(idx)->low() >= range.second))) {
            do_insert(idx, range, std::forward< Args >(args)...);
            return;
        }

        auto write_idx{idx};
        koffset_t pending_low{range.first};
        while ((idx <= m_max_entry_idx) && (get_nth_const(idx)->low() <= range.second)) {
            auto e{get_nth(idx)};
            if (e->is_valid) {
                if (pending_low != e->low()) { // If there is a gap in-between
                    do_insert(write_idx, {pending_low, e->low() - 1}, std::forward< Args >(args)...);
                    if (write_idx++ == idx) { e = get_nth(++idx); }
                }
                if (write_idx != idx) {
                    move_idx(write_idx, idx);
                    e = get_nth(write_idx);
                }

                update_cb(m_key, {e->low(), std::min(e->high(), range.second)}, e->get_value());
                pending_low = e->high() + 1;
                ++write_idx;
            }
            ++idx;
        }

        if (pending_low <= range.second) {
            do_insert(write_idx++, {pending_low, range.second}, std::forward< Args >(args)...);
        }
        assert(idx >= write_idx);
        m_max_entry_idx -= (idx - write_idx);
    }

    kcount_t erase(const koffset_range_t input_range, const auto& extract_subrange_cb, const auto& erase_cb = nullptr) {
        kcount_t erased_count{0};
        auto [idx, found] = binary_search(-1, s_cast< int >(num_entries()), input_range.first);
        if (idx > m_max_entry_idx) { goto done; }

        while (true) {
            val_entry_info* vinfo{get_nth(idx)};
            val_entry_info* new_vinfo{nullptr};
            if (vinfo->is_valid) {
                auto m = get_match_type(vinfo->range, input_range);
                LOGTRACE("input_range=[{}-{}] is {} of vinfo=[{}-{}]", input_range.first, input_range.second, m,
                         vinfo->range.first, vinfo->range.second);

                switch (m) {
                case match_type_t::exact:
                case match_type_t::superset:
                    if ((erase_cb == nullptr) || erase_cb(m_key, *vinfo)) {
                        // Entire entry is deleted, mark them as invalid
                        vinfo->is_valid = false;
                        erased_count += (vinfo->range.second - vinfo->range.first + 1);
                    }
                    break;

                case match_type_t::subset:
                    assert(num_entries() < alloced_entries()); // cannot have subset match if we cannot expand anymore
                    if ((erase_cb == nullptr) || erase_cb(m_key, *vinfo)) {
                        // Extract the post match entry and emplace them in a new expanded field position
                        shift_right(idx + 1, 1);
                        new_vinfo = get_nth(idx + 1);
                        new_vinfo->range = {input_range.second + 1, vinfo->range.second};
                        new_vinfo->is_valid = true;
                        extract_subrange_cb(vinfo->get_value(), adjust_offset(new_vinfo->range, vinfo->range.first),
                                            new_vinfo->get_raw_value());

                        // Extract the pre match entry and change that in-place
                        vinfo->range.second = input_range.first - 1;
                        extract_subrange_cb(vinfo->get_value(), adjust_offset(vinfo->range, vinfo->range.first),
                                            nullptr);
                        erased_count += (input_range.second - input_range.first + 1);
                        ++idx;
                    }
                    break;

                case match_type_t::pre_partial:
                    if ((erase_cb == nullptr) || erase_cb(m_key, *vinfo)) {
                        erased_count += (input_range.second - vinfo->range.first + 1);
                        vinfo->range = {input_range.second + 1, vinfo->range.second};
                        extract_subrange_cb(vinfo->get_value(), adjust_offset(vinfo->range, vinfo->range.first),
                                            nullptr);
                    }
                    break;

                case match_type_t::post_partial:
                    if ((erase_cb == nullptr) || erase_cb(m_key, *vinfo)) {
                        erased_count += (vinfo->range.second - input_range.first + 1);
                        vinfo->range = {vinfo->range.first, input_range.first - 1};
                        extract_subrange_cb(vinfo->get_value(), adjust_offset(vinfo->range, vinfo->range.first),
                                            nullptr);
                    }
                    break;

                case match_type_t::no_match_pre:
                case match_type_t::no_match_post:
                    goto done;
                    break;

                default:
                    assert(0);
                    break;
                }
            }

            // Check if upper bound of the input_range is within bounds of next entry.
            if (idx == m_max_entry_idx) break;
            found = (get_nth_const(++idx)->compare_range(input_range.second) >= 0);
        }

    done:
        m_valid_count -= erased_count;
        return erased_count;
    }

    std::string to_string() const {
        auto str = fmt::format("num_entries={} alloc_entries={} ", num_entries(), alloced_entries());
        koffset_t idx{0};
        while (true) {
            fmt::format_to(std::back_inserter(str), "idx={} [{}] ", idx, get_nth_const(idx)->to_string());
            if (s_cast< uint32_t >(idx) == m_max_entry_idx) { break; }
            ++idx;
        }
        return str;
    }

    void validate_keys() const {
        auto prev_entry{get_nth_const(0)->range};
        koffset_t idx{1};

        RELEASE_ASSERT_LE(prev_entry.first, prev_entry.second, "Incorrect from>to: idx=0");

        while (s_cast< uint32_t >(idx) < m_max_entry_idx) {
            auto vinfo = get_nth_const(idx);
            RELEASE_ASSERT_LE(vinfo->range.first, vinfo->range.second, "Incorrect from>to: idx={}", idx);

            if ((vinfo->compare_range(prev_entry.first) != -1) || (vinfo->compare_range(prev_entry.second) != -1)) {
                LOGFATAL("Incorrect - idx={} prev_entry=[{}-{}] cur_entry=[{}-{}]", idx, prev_entry.first,
                         prev_entry.second, vinfo->range.first, vinfo->range.second);
            }
            ++idx;
        }
    }

    kcount_t valid_count() const { return m_valid_count; }

private:
    std::pair< int, bool > binary_search(int start, int end, const koffset_t offset) const {
        int mid{0};
        while ((end - start) > 1) {
            mid = start + (end - start) / 2;
            int x = get_nth_const(mid)->compare_range(offset);
=======
    big_count_t insert(const RangeKey< K >& input_key, sisl::byte_view&& value) {
        const small_range_t input_range = to_relative_range(input_key);

        auto [l_idx, l_found] = binary_search(-1, static_cast< int >(m_values.size()), input_range.first);
        auto [r_idx, r_found] = binary_search(-1, static_cast< int >(m_values.size()), input_range.second);
        if (!l_found && !r_found) {
            // New entry
            DEBUG_ASSERT_EQ(l_idx, r_idx);
            m_values.insert(m_values.begin() + l_idx, value_info{input_range, std::move(value)});
            RangeHashMap< K >::call_access_cb(input_key, hash_op_t::CREATE, m_values[l_idx].m_context);
            return input_range.second - input_range.first;
        }

        if (l_found && r_found) {
            // Completely overlapped existing entry, nothing to be added.
            return 0;
        }

        if (l_found) {
            // Insert missing tail contents by extracting value
            const auto lbound = m_values[r_idx - 1].m_range.second + 1;
            const auto count = input_range.second - lbound + 1;
            const auto new_krange = small_range_t{lbound, input_range.second};
            m_values.insert(
                m_values.begin() + r_idx,
                value_info{new_krange,
                           RangeHashMap< K >::extract_value(std::move(value), lbound - input_range.first, count)});
            RangeHashMap< K >::call_access_cb(to_big_key(new_krange), hash_op_t::CREATE, m_values[r_idx].m_context);
            return count;
        } else {
            // Insert missing head contents by extracting value
            const auto ubound = m_values[l_idx].m_range.first - 1;
            const auto count = ubound - input_range.first + 1;
            const auto new_krange = small_range_t{input_range.first, ubound};
            m_values.insert(m_values.begin() + l_idx,
                            value_info{new_krange, RangeHashMap< K >::extract_value(std::move(value), 0, count)});
            RangeHashMap< K >::call_access_cb(to_big_key(new_krange), hash_op_t::CREATE, m_values[l_idx].m_context);
            return count;
        }
    }

    void erase(const RangeKey< K >& input_key) {
        const small_range_t input_range = to_relative_range(input_key);
        auto [l_idx, l_found] = binary_search(-1, static_cast< int >(m_values.size()), input_range.first);
        auto [r_idx, r_found] = binary_search(-1, static_cast< int >(m_values.size()), input_range.second);

        if (l_found) {
            auto& l_vinfo = m_values[l_idx];
            if (input_range.first > l_vinfo.m_range.first) {
                l_vinfo.m_val =
                    RangeHashMap< K >::extract_value(l_vinfo.m_val, 0u, input_range.first - l_vinfo.m_range.first);
                l_vinfo.m_range.second = input_range.first - 1;
                ++l_idx;
            }
        }

        if (r_found) {
            auto& r_vinfo = m_values[r_idx];
            if (input_range.second < r_vinfo.m_range.second) {
                auto erase_count = input_range.second - r_vinfo.m_range.first;
                r_vinfo.m_val =
                    RangeHashMap< K >::extract_value(r_vinfo.m_val, erase_count + 1, r_vinfo.count() - erase_count);
                r_vinfo.m_range.first = input_range.second + 1;
                --r_idx;
            }
        }

        // Delete everything in between
        if (r_idx > l_idx) { m_values.erase(m_values.begin() + l_idx, m_values.begin() + r_idx - l_idx); }
    }

private:
    std::pair< int, bool > binary_search(int start, int end, const small_offset_t offset) const {
        int mid{0};
        while ((end - start) > 1) {
            mid = start + (end - start) / 2;
            int x = m_values[mid].compare_range(offset);
>>>>>>> Stashed changes
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

<<<<<<< Updated upstream
    template < class... Args >
    val_entry_info* do_insert(koffset_t idx, koffset_range_t range, Args&&... args) {
        auto vinfo{get_nth(idx)};
        if (idx <= m_max_entry_idx) {
            if (vinfo->is_valid) { shift_right(idx, 1); }
        }

        LOGTRACE("Inserting in idx={} addr={}", idx, r_cast< void* >(vinfo->get_raw_value()));
        V* val = new (vinfo->get_raw_value()) V(std::forward< Args >(args)...);
        vinfo->is_valid = true;
        vinfo->range = range;
        m_valid_count += range.second - range.first + 1;
        return vinfo;
    }

    match_type_t get_match_type(const koffset_range_t base, const koffset_range_t cmp) const {
        if (cmp.first == base.first) {
            if (cmp.second == base.second) {
                return match_type_t::exact;
            } else if (cmp.second > base.second) {
                return match_type_t::superset;
            } else {
                return match_type_t::pre_partial;
            }
        }

        if (cmp.first > base.first) {
            if (cmp.first > base.second) {
                return match_type_t::no_match_post;
            } else if (cmp.second < base.second) {
                return match_type_t::subset;
            } else {
                return match_type_t::post_partial;
            }
        }

        // cmp.first < base.first
        if (cmp.second < base.first) {
            return match_type_t::no_match_pre;
        } else if (cmp.second >= base.second) {
            return match_type_t::superset;
        } else {
            return match_type_t::pre_partial;
        }
    }

    void shift_left(const koffset_t from, const koffset_t count) {
        assert(from >= count);
        LOGDEBUG("Shifting left to compact for count={} from={} to={} move_count={} total_entries_now={}", count, from,
                 from - count, num_entries() - from, num_entries() - count);
        memmove(r_cast< uint8_t* >(get_nth(from - count)), r_cast< const uint8_t* >(get_nth_const(from)),
                ((num_entries() - from) * val_entry_info::size()));
        m_max_entry_idx -= count;
    }

    void shift_right(const koffset_t from, const koffset_t count) {
        assert(num_entries() + count <= alloced_entries());
        if (from <= m_max_entry_idx) {
            LOGDEBUG("Shifting right to give room for addln={} from={} to={} count={} total_entries_now={}", count,
                     from, from + count, num_entries() - from, num_entries() + count);
            memmove(r_cast< uint8_t* >(get_nth(from + count)), r_cast< const uint8_t* >(get_nth_const(from)),
                    ((num_entries() - from) * val_entry_info::size()));
        }
        m_max_entry_idx += count;
    }

    void move_idx(const koffset_t to_idx, const koffset_t from_idx) {
        memmove(r_cast< uint8_t* >(get_nth(to_idx)), r_cast< const uint8_t* >(get_nth_const(from_idx)),
                val_entry_info::size());
        get_nth(from_idx)->is_valid = false;
    }

    val_entry_info* get_nth(const koffset_t nth) {
        return r_cast< val_entry_info* >(r_cast< uint8_t* >(&m_vinfo[0]) + (nth * val_entry_info::size()));
    }

    koffset_range_t adjust_offset(koffset_range_t r, koffset_t by) const {
        return koffset_range_t{r.first - by, r.second - by};
    }

    const val_entry_info* get_nth_const(const koffset_t nth) const {
        return r_cast< const val_entry_info* >(r_cast< const uint8_t* >(&m_vinfo[0]) + (nth * val_entry_info::size()));
    }

    uint32_t num_entries() const { return m_max_entry_idx + 1; }
    uint32_t alloced_entries() const { return m_max_alloc_idx + 1; }

    static size_t size(const uint32_t nentries) {
        return sizeof(MultiEntryHashNode< K, V >) - sizeof(val_entry_info) + (nentries * val_entry_info::size());
    }

    ValueView< K, V > to_value_view(const val_entry_info* vinfo) {
        ValueView< K, V > v;
        v.m_kview.m_base_key = m_key;
        v.m_kview.m_offset = vinfo->range.first;
        v.m_kview.m_size = vinfo->range.second - vinfo->range.first + 1;
        v.m_val = *vinfo->get_value();
        return v;
    }

private:
    K m_key;
    koffset_t m_max_entry_idx{0}; // Total number of entries, max 256
    koffset_t m_max_alloc_idx{0};
    kcount_t m_valid_count{0};
    val_entry_info m_vinfo[1];
};

///////////////////////////// HashBucket section //////////////////////////////////////////////
template < typename K, typename V >
class HashBucket {
    template < typename K, typename V >
    using MultiEntryHashNode< K, V > = HashNode;

public:
    HashBucket() {}
=======
    small_range_t to_relative_range(const RangeKey< K >& input_key) const {
        small_range_t range;
        range.first = input_key.m_nth - m_base_nth;
        range.second = input_key.end_nth() - m_base_nth;
        return range;
    }

    RangeKey< K > to_big_key(const small_range_t range) const {
        return RangeKey< K >{m_base_key, m_base_nth + range.first,
                             static_cast< uint32_t >(range.second) - range.first + 1};
    }

    std::pair< big_offset_t, big_offset_t > to_big_range(const small_range_t range) const {
        return std::make_pair<>(m_base_nth + range.first, m_base_nth + range.second);
    }

    std::pair< RangeKey< K >, sisl::byte_view > extract_matched_kv(const value_info& vinfo,
                                                                   const small_range_t& input_range) const {
        small_range_t key_range{std::max(vinfo.m_range.first, input_range.first),
                                std::min(vinfo.m_range.second, input_range.second)};
        auto val_start = key_range.first - vinfo.m_range.first;
        auto val_count = key_range.second - vinfo.m_range.first + 1;
        return std::make_pair<>(to_big_key(key_range),
                                RangeHashMap< K >::extract_value(vinfo.m_val, val_start, val_count));
    }

    sisl::byte_view extract_matched_value(const value_info& vinfo, const small_range_t& input_range) const {
        small_range_t key_range{std::max(vinfo.m_range.first, input_range.first),
                                std::min(vinfo.m_range.second, input_range.second)};
        auto val_start = key_range.first - vinfo.m_range.first;
        auto val_count = key_range.second - vinfo.m_range.first + 1;
        return RangeHashMap< K >::extract_value(vinfo.m_val, val_start, val_count);
    }
};

template < typename K >
thread_local sisl::RangeHashMap< K >* sisl::RangeHashMap< K >::s_cur_hash_map{nullptr};

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
>>>>>>> Stashed changes

    ~HashBucket() {
        auto it{m_list.begin()};
        while (it != m_list.end()) {
<<<<<<< Updated upstream
            HashNode* n = *it;
=======
            MultiEntryHashNode< K >* n = &*it;
>>>>>>> Stashed changes
            it = m_list.erase(it);
            delete n;
        }
    }

<<<<<<< Updated upstream
    template < class... Args >
    bool try_emplace(const KeyView& kview, Args&&... args) {
        HashNode* n{nullptr};
        hash_write_lock();
        auto it{m_list.begin()};
        for (auto itend{m_list.end()}; it != itend; ++it) {
            const int x{K::compare(it->m_key, kview.m_base_key)};
            if (x == 0) {
                n = get_writable_node(it);
                break;
            } else if (x > 0) {
                break;
            }
        }

        // Create a new node and insert the value in the node and to the list, if its not found
        if (n == nullptr) {
            n = HashNode::alloc_node(kview.m_base_key);
            m_list.insert(it, n);
        }

        const auto [vptr, is_emplaced] = {n->try_emplace(kview.range(), std::forward< Args >(args)...)};
        hash_write_unlock();
        return is_emplaced;
    }

    template < class... Args >
    void emplace_or_update(const KeyView& kview, const auto& update_cb, Args&&... args) {
        HashNode* n{nullptr};
        hash_write_lock();
        auto it{m_list.begin()};
        for (auto itend{m_list.end()}; it != itend; ++it) {
            const int x{K::compare(it->m_key, kview.m_base_key)};
            if (x == 0) {
                n = get_writable_node(it, (it->num_entries_in_range(kview.range()) * 2) + 1);
                break;
            } else if (x > 0) {
                break;
            }
        }

        // Create a new node and insert the value in the node and to the list, if its not found
        if (n == nullptr) {
            n = HashNode::alloc_node(kview.m_base_key);
            m_list.insert(it, n);
        }

        // TODO: Need to realloc node if needed and then adjust the m_list with this new value.
        n->emplace_or_update(kview.range(), update_cb, std::forward< Args >(args)...);
        hash_write_unlock();
    }

    uint32_t get(const KeyView& kview, std::vector< ValueView< K, V > >& out_values) const {
        uint32_t ret{0};

        hash_read_lock();
        for (const auto& it : m_list) {
            int x = K::compare(it->m_key, kview.m_base_key);
            if (x < 0) { continue; }
            ret = (x == 0) ? it->get(kview.range(), out_values) : 0;
            break;
        }
        hash_read_unlock();
        return ret;
    }

    uint32_t erase_if(const KeyView& kview, const auto& extract_subrange_cb, const auto& erase_cb) {
        kcount_t erased_count{0};

        hash_write_lock();
        auto it{m_list.begin()};
        for (auto itend{m_list.end()}; it != itend; ++it) {
            int x = K::compare(it->m_key, kview.m_base_key);
            if (x == 0) {
                HashNode* n = get_writeable_node(it);
                erased_count = n->erase(kview.range(), extract_subrange_cb, erase_cb);
                if (erased_count && (n->valid_count() == 0)) {
                    it = m_list.erase(it);
                    delete n;
                }
                break;
            } else if (x > 0) {
                break;
            }
            prev_n = n;
        }
        hash_write_unlock();
        return erased_count;
    }

    uint64_t get_size() const { return m_list.size(); }

private:
    HashNode* get_writable_node(auto& it, koffset_t num_entries = 1) {
        auto [n, resized] = HashNode::resize_if_needed(&*it, num_entries);
        if (resized) {
            auto next_it = m_list.erase(it);
            m_list.insert(next_it, n);
            it = iterator_to(n);
        }
        return n;
    }

    void lock(bool shared) const {
#ifndef GLOBAL_HASHSET_LOCK
        if (shared) {
            m_lock.lock_shared();
        } else {
            m_lock.lock();
        }
#endif
    }

    void unlock(bool shared) const {
#ifndef GLOBAL_HASHSET_LOCK
        if (shared) {
            m_lock.unlock_shared();
        } else {
            m_lock.unlock();
        }
#endif
    }

private:
#ifndef GLOBAL_HASHSET_LOCK
    mutable folly::SharedMutexReadPriority m_lock;
#endif
    typedef boost::intrusive::slist< HashNode > hash_node_list_t;
    hash_node_list_t m_list;
};

////////////// Range HashMap implementation /////////////////
template < typename K, typename V >
class RangeHashMap {
private:
    static thread_local std::vector< KeyView > s_kviews;

public:
    RangeHashMap(uint32_t nBuckets) : m_size{0}, m_nbuckets{nBuckets} { m_buckets = new HashBucket< K, V >[nBuckets]; }
    ~RangeHashMap() { delete[] m_buckets; }

    template < class... Args >
    bool try_emplace(const K& k, Args&&... args) {
        uint32_t num_emplaced{0};
        bool is_done{false};
#ifdef GLOBAL_HASHSET_LOCK
        std::lock_guard< std::mutex > lk(m);
#endif
        s_kviews.clear();
        K::split(k, MultiEntryHashNode< K, V >::max_alloc_nentries(), s_kviews);
        for (const auto& kview : s_kviews) {
            HashBucket< K, V >* hb = get_bucket(kview.m_base_key);
            is_done = hb->try_emplace(kview, std::forward< Args >(args)...);
            if (!is_done) { break; }
            ++num_emplaced;
        }

        return is_done;
    }

    template < class... Args >
    void emplace_or_update(const K& k, const auto& update_cb, Args&&... args) {
#ifdef GLOBAL_HASHSET_LOCK
        std::lock_guard< std::mutex > lk(m);
#endif
        s_kviews.clear();
        K::split(k, MultiEntryHashNode< K, V >::max_alloc_nentries(), s_kviews);
        for (const auto& kview : s_kviews) {
            HashBucket< K, V >* hb = get_bucket(kview.m_base_key);
            hb->emplace_or_update(kview, update_cb, std::forward< Args >(args)...);
        }

        return is_done;
    }

    uint32_t get(const K& k, std::vector< ValueView< K, V > >& out_values) const {
        uint32_t count{0};
#ifdef GLOBAL_HASHSET_LOCK
        std::lock_guard< std::mutex > lk(m);
#endif

        // Split the keys into max entry a node can have.
        s_kviews.clear();
        K::split(k, MultiEntryHashNode< K, V >::max_alloc_nentries(), s_kviews);
        for (const auto& kview : s_kviews) {
            HashBucket< K, V >* hb{get_bucket(kview.m_base_key)};
            count += hb->get(kview, out_values);
        }
        return count;
    }

    uint32_t erase_if(const KeyView& kview, const auto& extract_subrange_cb, const auto& erase_cb) {
        uint32_t num_erased{0};
#ifdef GLOBAL_HASHSET_LOCK
        std::lock_guard< std::mutex > lk(m);
#endif
        s_kviews.clear();
        K::split(k, MultiEntryHashNode< K, V >::max_alloc_nentries(), s_kviews);
        for (const auto& kview : s_kviews) {
            HashBucket< K, V >* hb = get_bucket(kview.m_base_key);
            num_erased += hb->erase(kview, extract_subrange_cb, erase_cb);
        }

        return num_erased;
    }

    uint32_t erase(const KeyView& kview, const auto& extract_subrange_cb) {
        return erase_if(kview, extract_subrange_cb);
    }

private:
    HashBucket< K, V >* get_bucket(const K& k) const { return &(m_buckets[compute_hash(k) % m_nbuckets]); }
    HashBucket< K, V >* get_bucket(uint64_t hash_code) const { return &(m_buckets[hash_code % m_nbuckets]); }

private:
    uint32_t m_nbuckets;
    HashBucket< K, V >* m_buckets;
#ifdef GLOBAL_HASHSET_LOCK
    mutable std::mutex m;
#endif
};
} // namespace sisl
=======
    big_count_t insert(const RangeKey< K >& input_key, sisl::byte_view&& value) {
#ifndef GLOBAL_HASHSET_LOCK
        folly::SharedMutexWritePriority::WriteHolder holder(m_lock);
#endif
        MultiEntryHashNode< K >* n = nullptr;
        auto it = m_list.begin();
        for (auto itend{m_list.end()}; it != itend; ++it) {
            if (input_key.m_base_key < it->m_base_key) {
                continue;
            } else if (input_key.m_base_key == it->m_base_key) {
                n = &*it;
            }
            break;
        }

        if (n == nullptr) {
            n = new MultiEntryHashNode< K >(input_key.m_base_key, input_key.rounded_nth());
            m_list.insert(it, *n);
        }
        return n->insert(input_key, std::move(value));
    }

    big_count_t get(const RangeKey< K >& input_key,
                    std::vector< std::pair< RangeKey< K >, sisl::byte_view > >& out_values) {
#ifndef GLOBAL_HASHSET_LOCK
        folly::SharedMutexWritePriority::ReadHolder holder(m_lock);
#endif
        big_count_t ret = 0;
        for (const auto& n : m_list) {
            if (input_key.m_base_key < n.m_base_key) { continue; }
            ret = (input_key.m_base_key == n.m_base_key) ? n.get(input_key, out_values) : 0;
            break;
        }
        return ret;
    }

    void erase(const RangeKey< K >& input_key) {
#ifndef GLOBAL_HASHSET_LOCK
        folly::SharedMutexWritePriority::WriteHolder holder(m_lock);
#endif
        MultiEntryHashNode< K >* n = nullptr;
        auto it = m_list.begin();
        for (auto itend{m_list.end()}; it != itend; ++it) {
            if (input_key.m_base_key < it->m_base_key) {
                continue;
            } else if (input_key.m_base_key == it->m_base_key) {
                n = &*it;
            }
            break;
        }
        if (n) { n->erase(input_key); }
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

////////////// Range HashMap implementation /////////////////
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
big_count_t RangeHashMap< K >::insert(const RangeKey< K >& input_key, const sisl::io_blob& value) {
#ifdef GLOBAL_HASHSET_LOCK
    std::lock_guard< std::mutex > lk(m);
#endif
    set_current_instance(this);
    big_count_t ret{0};
    auto cur_key_nth = input_key.m_nth;
    auto cur_val_nth = 0;
    RangeKey< K > node_key = input_key; // TODO: Can optimize this by avoiding base_key copy by doing some sort of view
    const sisl::byte_view base_val{value};

    while (cur_key_nth <= input_key.end_nth()) {
        const auto count = std::min(max_n_per_node, input_key.end_nth() - cur_key_nth + 1);
        node_key.m_nth = cur_key_nth;
        node_key.m_count = count;
        auto& hb = get_bucket(node_key);

        sisl::byte_view node_val = m_value_extractor(base_val, cur_val_nth, count);
        ret += hb.insert(node_key, std::move(node_val));

        cur_key_nth += count;
        cur_val_nth += count;
    }
    return ret;
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
    RangeKey< K > node_key = input_key; // TODO: Can optimize this by avoiding base_key copy by doing some sort of view

    while (cur_key_nth <= input_key.end_nth()) {
        const auto count = std::min(max_n_per_node, input_key.end_nth() - cur_key_nth + 1);
        node_key.m_nth = cur_key_nth;
        node_key.m_count = count;

        auto& hb = get_bucket(node_key);
        hb.get(node_key, out_vals);

        cur_key_nth += count;
        cur_val_nth += count;
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
    RangeKey< K > node_key = input_key; // TODO: Can optimize this by avoiding base_key copy by doing some sort of view

    while (cur_key_nth <= input_key.end_nth()) {
        const auto count = std::min(max_n_per_node, input_key.end_nth() - cur_key_nth + 1);
        node_key.m_nth = cur_key_nth;
        node_key.m_count = count;
        auto& hb = get_bucket(node_key);
        hb.erase(node_key);
        cur_key_nth += count;
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
>>>>>>> Stashed changes
