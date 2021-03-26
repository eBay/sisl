#pragma once

#include <boost/intrusive/slist.hpp>
#include <vector>
#include <string>
#include <folly/Traits.h>
#include <fds/utils.hpp>
#include <utility/enum.hpp>

#define DECLARE_RELOCATABLE(T)                                                                                         \
    namespace folly {                                                                                                  \
    template <>                                                                                                        \
    FOLLY_ASSUME_RELOCATABLE(T);                                                                                       \
    }

#if 0
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
#endif

namespace sisl {

typedef uint8_t koffset_t;
typedef std::pair< koffset_t, koffset_t > koffset_range_t;

ENUM(match_type_t, uint8_t, no_match, exact, superset, subset, pre_partial, post_partial)

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
    };

public:
    static MultiEntryHashNode< K, V >* alloc_node(const uint32_t nentries = min_alloc_nentries) {
        assert((nentries > 0) && (nentries <= max_alloc_nentries));
        uint8_t* buf{new uint8_t[size(nentries)]};
        auto n = new (buf) MultiEntryHashNode< K, V >();
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

        LOGINFO("Realloc node={} with cur_entries={} cur_size={} cur_alloc_entries={} to new_node={} new_size={} "
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

    template < class... Args >
    std::pair< V*, bool > try_emplace(const koffset_range_t range, Args&&... args) {
        // Ensure that start and end of range is found with valid values
        auto [start_idx, start_found] = binary_search(-1, s_cast< int >(num_entries()), range.first);
        if (start_found && get_nth(start_idx)->is_valid) {
            return std::make_pair<>(get_nth(start_idx)->get_value(), false);
        }

        int idx{start_idx + 1};
        int overwrite_idx{start_found ? start_idx : -1};
        while (idx <= m_max_entry_idx) {
            int x = get_nth(idx)->compare_range(range.second);
            if (x != 0) {
                assert(x < 0);
                break;
            }
            if (get_nth(idx)->is_valid) { return std::make_pair<>(get_nth(start_idx)->get_value(), false); }
            if (overwrite_idx == -1) { overwrite_idx = idx; }
            ++idx;
        }

        // Compact any invalid entries between the 2 ranges
        const auto compact_count{idx - start_idx - 1};
        if (compact_count) { shift_left(idx, compact_count); }

        val_entry_info* vinfo;
        if (overwrite_idx == -1) {
            // Nothing to overwrite, make some room and write the data in
            shift_right(start_idx, 1);
            vinfo = get_nth(start_idx);
            LOGINFO("Inserting in new idx={} addr={}", start_idx, r_cast< void* >(vinfo->get_raw_value()));
        } else {
            vinfo = get_nth(overwrite_idx);
            LOGINFO("Inserting in overrite idx={} addr={}", overwrite_idx, r_cast< void* >(vinfo->get_raw_value()));
        }

        V* val = new (vinfo->get_raw_value()) V(std::forward< Args >(args)...);
        vinfo->is_valid = true;
        vinfo->range = range;
        return std::make_pair<>(val, true);
    }

    koffset_t erase(const koffset_range_t input_range, const auto& extract_subrange_cb) {
        koffset_t erased_count{0};
        auto [idx, found] = binary_search(-1, s_cast< int >(num_entries()), input_range.first);
        while (found) {
            val_entry_info* vinfo{get_nth(idx)};
            val_entry_info* new_vinfo{nullptr};
            if (vinfo->is_valid) {
                auto m = get_match_type(vinfo->range, input_range);
                switch (m) {
                case match_type_t::exact:
                case match_type_t::superset:
                    // Entire entry is deleted, mark them as invalid
                    vinfo->is_valid = false;
                    erased_count += (vinfo->range.second - vinfo->range.first + 1);
                    break;

                case match_type_t::subset:
                    assert(num_entries() < alloced_entries()); // cannot have subset match if we cannot expand anymore

                    // Extract the post match entry and emplace them in a new expanded field position
                    shift_right(idx + 1, 1);
                    new_vinfo = get_nth(idx + 1);
                    new_vinfo->range = {input_range.second + 1, vinfo->range.second};
                    new_vinfo->is_valid = true;
                    extract_subrange_cb(vinfo->get_value(), adjust_offset(new_vinfo->range, vinfo->range.first),
                                        new_vinfo->get_raw_value());

                    // Extract the pre match entry and change that in-place
                    vinfo->range.second = input_range.first - 1;
                    extract_subrange_cb(vinfo->get_value(), adjust_offset(vinfo->range, vinfo->range.first), nullptr);
                    erased_count += (input_range.second - input_range.first + 1);

                    ++idx;
                    break;

                case match_type_t::pre_partial:
                    erased_count += (input_range.second - vinfo->range.first + 1);
                    vinfo->range = {input_range.second + 1, vinfo->range.second};
                    extract_subrange_cb(vinfo->get_value(), adjust_offset(vinfo->range, vinfo->range.first), nullptr);
                    break;

                case match_type_t::post_partial:
                    erased_count += (vinfo->range.second - input_range.first + 1);
                    vinfo->range = {vinfo->range.first, input_range.first - 1};
                    extract_subrange_cb(vinfo->get_value(), adjust_offset(vinfo->range, vinfo->range.first), nullptr);
                    break;

                case match_type_t::no_match:
                default:
                    assert(0);
                    break;
                }
            }
            // Check if upper bound of the input_range is within bounds of next entry.
            if (idx == m_max_entry_idx) break;
            found = (get_nth_const(++idx)->compare_range(input_range.second) >= 0);
        }
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

private:
    std::pair< int, bool > binary_search(int start, int end, const koffset_t offset) const {
        int mid{0};
        while ((end - start) > 1) {
            mid = start + (end - start) / 2;
            int x = get_nth_const(mid)->compare_range(offset);
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

    match_type_t get_match_type(const koffset_range_t left, const koffset_range_t right) {
        if (left.first == right.first) {
            if (left.second == right.second) {
                return match_type_t::exact;
            } else if (left.second < right.second) {
                return match_type_t::superset;
            } else {
                return match_type_t::subset;
            }
        }

        if (left.first < right.first) {
            if (left.second >= right.second) {
                return match_type_t::subset;
            } else if (left.second >= right.first) {
                return match_type_t::post_partial;
            } else {
                return match_type_t::no_match;
            }
        }

        // left.first > right.first
        if (left.second < right.second) {
            return match_type_t::superset;
        } else if (left.first <= right.second) {
            return match_type_t::pre_partial;
        } else {
            return match_type_t::no_match;
        }
    }

    void shift_left(const koffset_t to, const koffset_t count) {
        assert(to + count <= m_max_entry_idx);
        memmove(r_cast< uint8_t* >(get_nth(to)), r_cast< uint8_t* >(get_nth(to + count)),
                ((num_entries() - to - count) * val_entry_info::size()));
        m_max_entry_idx -= count;
    }

    void shift_right(const koffset_t from, const koffset_t count) {
        assert(num_entries() + count <= alloced_entries());
        LOGINFO("Shifting right to give room for addln={} from={} to={} count={} total_entries_now={}", count, from,
                from + count, num_entries() - from, num_entries() + count);
        memmove(r_cast< uint8_t* >(get_nth(from + count)), r_cast< uint8_t* >(get_nth(from)),
                ((num_entries() - from) * val_entry_info::size()));
        m_max_entry_idx += count;
    }

    val_entry_info* get_nth(const koffset_t nth) {
        return r_cast< val_entry_info* >(r_cast< uint8_t* >(&m_vinfo[0]) + (nth * val_entry_info::size()));
    }

    koffset_range_t adjust_offset(koffset_range_t r, koffset_t by) {
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

private:
    K m_key;
    koffset_t m_max_entry_idx{0}; // Total number of entries, max 256
    koffset_t m_max_alloc_idx{0};
    val_entry_info m_vinfo[1];
};

} // namespace sisl