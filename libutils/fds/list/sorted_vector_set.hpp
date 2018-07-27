/*
 * sorted_vector.hpp
 *
 *  Created on: 13-May-2017
 *      Author: hkadayam
 */

#ifndef SRC_LIBUTILS_FDS_SORTED_VECTOR_HPP_
#define SRC_LIBUTILS_FDS_SORTED_VECTOR_HPP_

#include <boost/dynamic_bitset.hpp>
#include <vector>
#include <boost/variant.hpp>
#include <iostream>
#include <glog/logging.h>

namespace fds {
template <typename K, typename V, typename Lesser, typename Equals>
class SortedVectorSet {
    friend class iterator;

public:
    SortedVectorSet(uint32_t lomark, uint32_t himark) :
            m_lo_watermark(lomark),
            m_hi_watermark(std::max(lomark, himark)),
            m_ndirty(0),
            m_nentries(0),
            m_busy_slots(himark) {
        m_vec.reserve(lomark);
    }

    size_t size() const {
        return m_nentries;
    }

    size_t dirty_size() const {
        return m_ndirty;
    }

    size_t memory_size() const {
        return sizeof(*this) + (m_vec.capacity() * sizeof(boost::variant<K, V>)) + m_busy_slots.capacity()/8;
    }

    size_t capacity() const {
        auto cap = m_vec.capacity();
        return std::max(m_vec.capacity(), m_busy_slots.capacity());
    }

    void resize(uint32_t nsize) {
        m_vec.resize(nsize);
        m_busy_slots.resize(nsize);
    }

#if 0
    bool insert(K &key, V &value)
    {
        bool isfound = false;
        uint32_t ind = bsearch(-1, m_vec.size(), key, &isfound);
        if (isfound == true) {
            return false;
        }

        if (ind == m_vec.size()) {
            m_vec.push_back(value);
            m_gcbits.push_back(1);
        } else {
            m_vec.insert(m_vec.begin() + ind, value);
            insert_bit(ind, 1);
        }
        return true;
    }
#endif

    bool insert_from_back(K &key, V &value) {
        auto vec_size = m_vec.size();
        // This is an extreme situation, we need to compact immediately
        // otherwise it resizes every time.
        if ( (vec_size == 0) || (is_lesser((vec_size-1), key))) {
            if ((vec_size != 0) && (is_equal((vec_size-1), key))) {
                return false;
            }

            m_vec.push_back(value);
            auto capacity = m_vec.capacity();
            if (capacity > m_busy_slots.size()) {
                m_busy_slots.resize(capacity, 0);
            }

            m_busy_slots[vec_size] = true;
            m_nentries++;

            // Compact if needed
            if (need_compaction()) compact();
            return true;
        } else {
            // Right now we do not support random inserts, until the
            // insertion to a bitset is figured out
            assert(0);
            return false;
#if 0
            // Not the last, do a search and insert.
            return (insert(key, value));
#endif
        }
    }

    bool find(K &key, V *outv) const {
        bool isfound;
        uint32_t ind = bsearch(-1, m_vec.size(), key, &isfound);
        if (!isfound) { return false; }
        if (outv != nullptr) { *outv = boost::get<V>(m_vec[ind]); }
        return true;
    }

    // Find the key and do not copy, rather return reference to the value
    bool find(K &key, V **outv) const {
        bool isfound;
        uint32_t ind = bsearch(-1, m_vec.size(), key, &isfound);
        if (!isfound) { return false; }
        if (outv != nullptr) { **outv = &boost::get<V>(m_vec[ind]); }
        return true;
    }

    bool extract(K &key, V *outv) {
        bool isfound;
        uint32_t ind = bsearch(-1, m_vec.size(), key, &isfound);
        if (!isfound) { return false; }

        if (outv != nullptr) { *outv = boost::get<V>(m_vec[ind]); }

        // Store the key in that space. Free up the slot which will be gc'ed later
        m_vec[ind] = key;
        m_busy_slots[ind] = false;
        m_ndirty++;
        m_nentries--;

        if (need_compaction()) compact();
        return true;
    }

    uint32_t compact() {
        uint32_t n_gcd = 0;

        //LOG(INFO) << "Sorted vector set: " << m_ndirty << " dirty entries trigger compaction, Before compact = " << to_string();

        // Find the first free slot in the set, by doing a flip of bits and search for first 1.
        auto left_ind = (~m_busy_slots).find_first();
        if (left_ind == boost::dynamic_bitset<>::npos) {
            // All slots are busy, nothing to compact
            return 0;
        }

        auto right_ind = left_ind;
        while ((right_ind = m_busy_slots.find_next(right_ind)) != boost::dynamic_bitset<>::npos) {
            m_vec[left_ind] = m_vec[right_ind];
            m_busy_slots[right_ind] = false;
            m_busy_slots[left_ind++] = true;
        }

        n_gcd = m_vec.size() - left_ind;
        if (n_gcd > 0) {
            m_busy_slots.resize(std::max((uint32_t)left_ind, m_hi_watermark));
            m_vec.resize(left_ind);
            (m_vec.capacity() > m_hi_watermark) ? m_vec.shrink_to_fit() :
                                                  m_vec.reserve(std::max((uint32_t)left_ind, m_lo_watermark));

            assert(m_ndirty >= n_gcd);
            m_ndirty -= n_gcd;
        }

        //LOG(INFO) << "Sorted vector set: After compacting " << n_gcd << " entries: " << to_string();
        return n_gcd;
    }

    template <typename K2, typename V2, typename Lesser2, typename Equals2>
    class iterator {
      public:
        iterator(SortedVectorSet<K2, V2, Lesser2, Equals2>* set, uint32_t ind) {
            m_set = set;
            m_ind = ind;
            set_position();
        }

        bool operator==(const iterator<K2, V2, Lesser2, Equals2>& other) const { return (m_ind == other.m_ind); }

        bool operator!=(const iterator<K2, V2, Lesser2, Equals2>& other) const { return (m_ind != other.m_ind); }

        V& operator*() { return boost::get<V2>(m_set->m_vec[m_ind]); }

        iterator& operator++() {
            m_ind++;
            set_position();
            return *this;
        }

        iterator operator++(int) {
            m_ind++;
            set_position();
            return *this;
        }

        void set_position() {
            do {
                if ((m_ind == m_set->m_busy_slots.size()) || m_ind == m_set->m_vec.size() ||
                        (m_set->m_busy_slots[m_ind] == true)) {
                    break;
                }
                size_t ind = m_set->m_busy_slots.find_next(m_ind);
                if (ind == boost::dynamic_bitset<>::npos) {
                    m_ind = m_set->m_vec.size();
                    break;
                } else {
                    m_ind = ind;
                }
            } while (true);
        }

      private:
        SortedVectorSet<K, V, Lesser, Equals>* m_set;
        uint32_t m_ind;
    };

    iterator<K, V, Lesser, Equals> begin() { return iterator<K, V, Lesser, Equals>(this, 0); }

    iterator<K, V, Lesser, Equals> end() { return iterator<K, V, Lesser, Equals>(this, m_vec.size()); }

    // Find which returns an iterator
    iterator<K, V, Lesser, Equals> find(K& key) {
        bool isfound;
        uint32_t ind = bsearch(-1, m_vec.size(), key, &isfound);
        if (!isfound) { return false; }
        iterator<K, V, Lesser, Equals> it(this, ind);
        return it;
    }

    // Extract the key from a given iterator position
    bool extract(K &key, iterator<K, V, Lesser, Equals> &iter, V *outv) {
        assert(m_busy_slots[iter.m_ind] == true);
        if (!is_equal(iter.m_ind, key)) {
            return false;
        }

        if (outv != nullptr) {
            *outv = boost::get<V>(m_vec[iter.m_ind]);
        }
        m_vec[iter.m_ind] = key;
        m_busy_slots[iter.m_ind] = false;
        m_ndirty++;
        m_nentries--;

        return true;
    }

    std::string to_string() const {
        std::stringstream ss;
        ss << "Total entries = " << m_nentries << " dirty entries = " << m_ndirty
           << " vector size = " << m_vec.size() << " vector capacity = " << m_vec.capacity()
           << " bitset size = " << m_busy_slots.size() << " bitset capacity " << m_busy_slots.capacity();
        return ss.str();
    }


#ifdef TEST_MODE
public:
#else
private:
#endif
    uint32_t bsearch(int start, int end, K &key, bool *found) const {
        int mid = 0;
        *found = false;

        while ((end - start) > 1) {
            mid = start + (end - start) / 2;
            if (is_equal(mid, key)) {
                *found = true;
                return mid;
            } else if (is_lesser(mid, key)) {
                start = mid;
            } else {
                end = mid;
            }
        }

        assert(end >= 0);
        return (uint32_t)(end);
    }

    bool need_compaction() const {
        if (m_vec.size() >= m_hi_watermark) {
            // If total reached hi water mark, we should consider compacting.
            // However, do not compact if dirty is at least equal to lowater mark.
            // Otherwise, this will lead to terrible compaction performance.
            return (m_ndirty >= m_lo_watermark);
        } else {
            return (m_ndirty >= dirty_limit());
        }
    }

    inline uint32_t dirty_limit() const {
        // At least reach 75% of the buffer between hi and lo water mark.
        return (m_lo_watermark + ((float)(m_hi_watermark - m_lo_watermark)) * 0.75);
    }

#if 0
    void insert_bit(uint32_t ind, bool value)
    {
        // In case we are at the brink in capacity,
        // simply push a 0 bit so that last bit has a room
        // after left shift.
        if (m_gcbits.size() == m_vec.size()) {
            m_gcbits.push_back(0);
        }

        // Shift every bit by 1
        m_gcbits <<=1;
        m_gcbits >>=ind;

        // Store the last bit which has data in it
        bool last_bit = m_gcbits[m_gcbits.size() - 1];
    }
#endif

    inline bool is_removed(uint32_t ind) const {
        return (m_busy_slots[ind] == false);
    }

    inline bool is_equal(uint32_t ind, const K &key) const {
        if (is_removed(ind)) {
            return false;
        }
        return Equals()(boost::get<V>(m_vec[ind]), key);
    }

    inline bool is_lesser(uint32_t ind, const K &key) const {
        if (is_removed(ind)) {
            return Lesser()(boost::get<K>(m_vec[ind]), key);
        } else {
            return Lesser()(boost::get<V>(m_vec[ind]), key);
        }
    }

#ifdef TEST_MODE
public:
#else
private:
#endif

    // Low water mark, we expect to allocate at least this much
    uint32_t m_lo_watermark;

    // We don't want to allocate anything more than this.
    uint32_t m_hi_watermark;

    // Total number of dirty entries
    uint32_t m_ndirty;

    // Total number of actual entries
    uint32_t m_nentries;

    // Vector which holds the set of values or keys in case value is removed
    std::vector<boost::variant<K, V>> m_vec;

    // A bitset where 1 marks for valid data and 0 for removed
    boost::dynamic_bitset<> m_busy_slots;
};

} // namespace fds

#endif /* SRC_LIBUTILS_FDS_SORTED_VECTOR_HPP_ */
