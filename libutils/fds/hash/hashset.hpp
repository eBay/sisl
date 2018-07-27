/*
 * HashTable.hpp
 *
 *  Created on: 21-Dec-2016
 *      Author: hkadayam
 */
#ifndef __LIBUTILS_FDS_HASHSET_HPP__
#define __LIBUTILS_FDS_HASHSET_HPP__

#include <assert.h>
#include <atomic>
#include <memory>
#include <farmhash.h>
#ifdef GLOBAL_HASHSET_LOCK
#include <mutex>
#endif

namespace fds {

enum locktype { lock_none, lock_read, lock_write };

class HashKey {
  public:
    HashKey(uint8_t* bytes, int len) {
        m_bytes = bytes;
        m_len = len;
        m_hash_code = util::Hash32((const char*)bytes, (size_t)len);
    }

    HashKey(uint8_t* bytes, int len, uint64_t hash_code) {
        m_bytes = bytes;
        m_len = len;
        m_hash_code = hash_code;
    }

    ~HashKey() {}

    uint64_t get_hash_code() const { return m_hash_code; }

    int compare(const HashKey& other) const {
        int cmplen = std::min(m_len, other.m_len);
        int x = memcmp(m_bytes, other.m_bytes, cmplen);

        if (x == 0) {
            return (other.m_len - m_len);
        } else {
            return x;
        }
    }

    const uint8_t* get_key(uint32_t* outlen) const {
        *outlen = m_len;
        return m_bytes;
    }

  private:
    uint8_t* m_bytes;
    uint32_t m_len;
    uint64_t m_hash_code;
};

class HashNode {
  public:
    HashNode() : m_next_in_list(nullptr) { m_refcount.store(0, std::memory_order_relaxed); }

    virtual ~HashNode() {
        uint32_t refcount = count_ref();
        assert((refcount == 0) || (refcount == 1));
    }

    HashNode* get_next() const { return m_next_in_list.get(); }

    std::unique_ptr<HashNode> get_unique_next() {
        auto ptr = std::move(m_next_in_list);
        return ptr;
    }

    void set_next(std::unique_ptr<HashNode> v) { m_next_in_list = std::move(v); }

    void ref() { m_refcount.fetch_add(1, std::memory_order_relaxed); }

    void deref() { m_refcount.fetch_sub(1, std::memory_order_relaxed); }

    uint32_t count_ref() { return m_refcount.load(std::memory_order_relaxed); }

    virtual const HashKey* extract_key() const = 0;
    virtual int compare(const HashKey& k) const {
        const HashKey* pk = extract_key();
        return pk->compare(k);
    }

  private:
    std::unique_ptr<HashNode> m_next_in_list;
    std::atomic<uint32_t> m_refcount;
};

////////////// hash_bucket implementation /////////////////
class HashBucket {
  public:
    HashBucket() : m_first_value(nullptr) { pthread_rwlock_init(&m_lock, nullptr); }

    ~HashBucket() {
        pthread_rwlock_destroy(&m_lock);

        // We are doing this to avoid stack getting deeper when
        // we release unique_ptr in the chain
        std::unique_ptr<HashNode> next_unode = std::move(m_first_value);
        std::unique_ptr<HashNode> cur_unode;
        while (next_unode != nullptr) {
            cur_unode = std::move(next_unode);
            next_unode = cur_unode->get_unique_next();
        }
    }
    bool insert(const HashKey& k, std::unique_ptr<HashNode> v, HashNode** outv, std::unique_ptr<HashNode>* retback) {
        bool found = false;
        HashNode* prev;
        HashNode* cursor;

        lock(lock_write);
        found = find(k, &prev, &cursor);
        if (found) {
            cursor->ref();
            *outv = cursor;
            if (retback) { *retback = std::move(v); }
        } else {
            v->ref();
            *outv = v.get();
            if (prev == nullptr) {
                v->set_next(std::move(m_first_value));
                m_first_value = std::move(v);
            } else {
                auto p = prev->get_unique_next();
                v->set_next(std::move(p));
                prev->set_next(std::move(v));
            }
        }

        unlock();
        return !found;
    }

    bool get(const HashKey& k, HashNode** outv) {
        bool found = false;
        HashNode* prev;
        HashNode* cursor;

        lock(lock_read);
        found = find(k, &prev, &cursor);
        if (found) {
            cursor->ref();
            *outv = cursor;
        } else {
            *outv = nullptr;
        }

        unlock();
        return found;
    }

    void foreach(std::function<void(const HashNode *)> callback) {
        lock(lock_read);
        const HashNode *node = m_first_value.get();
        while (node != nullptr) {
            callback(node);
            node = node->get_next();
        }
        unlock();
    }

    bool remove(const HashKey &k, bool *removed, std::unique_ptr<HashNode> *outv) {
        bool found = false;
        HashNode* prev;
        HashNode* cursor;

        *removed = false;
        lock(lock_write);
        found = find(k, &prev, &cursor);
        if (found) {
            if (cursor->count_ref() > 1) {
                cursor->deref();
            } else if (cursor == m_first_value.get()) {
                assert(prev == nullptr);
                if (outv != nullptr) { *outv = std::move(m_first_value); }
                m_first_value = cursor->get_unique_next();
                *removed = true;
            } else {
                if (outv != nullptr) { *outv = prev->get_unique_next(); }

                prev->set_next(cursor->get_unique_next());
                *removed = true;
            }
        } else {
            *outv = nullptr;
        }

        unlock();
        return found;
    }

    bool release(HashKey& k) {
        bool removed;
        return remove(k, &removed, nullptr);
    }

    bool release(HashNode* v) {
        bool removed;
        return remove(*(v->extract_key()), &removed, nullptr);
    }

  private:
#ifndef GLOBAL_HASHSET_LOCK
    pthread_rwlock_t m_lock;
#endif
    std::unique_ptr<HashNode> m_first_value;

    bool find(const HashKey& k, HashNode** prev, HashNode** cursor) {
        bool found = false;

        *cursor = m_first_value.get();
        *prev = nullptr;

        while (*cursor != nullptr) {
            int x = (*cursor)->compare(k);
            if (x == 0) {
                found = true;
                break;
            } else if (x > 0) {
                found = false;
                break;
            }

            *prev = *cursor;
            *cursor = (*cursor)->get_next();
        }

        return found;
    }

    void lock(locktype ltype) {
#ifndef GLOBAL_HASHSET_LOCK
        if (ltype == lock_write) {
            pthread_rwlock_wrlock(&m_lock);
        } else if (ltype == lock_read) {
            pthread_rwlock_rdlock(&m_lock);
        }
#endif
    }

    void unlock() {
#ifndef GLOBAL_HASHSET_LOCK
        pthread_rwlock_unlock(&m_lock);
#endif
    }
};

////////////// hash_table implementation /////////////////
class HashSet {
  public:
    HashSet(uint32_t nBuckets) {
        m_nBuckets = nBuckets;
        m_buckets = new HashBucket[nBuckets];
    }

    ~HashSet() { delete[] m_buckets; }

    bool insert(std::unique_ptr<HashNode> v, HashNode** outv, std::unique_ptr<HashNode>* retback) {
#ifdef GLOBAL_HASHSET_LOCK
        std::lock_guard<std::mutex> lk(m);
#endif
        const HashKey* pk = v->extract_key();
        HashBucket* hb = get_bucket(*pk);
        return (hb->insert(*pk, std::move(v), outv, retback));
    }

    bool get(const HashKey& k, HashNode** outv) {
#ifdef GLOBAL_HASHSET_LOCK
        std::lock_guard<std::mutex> lk(m);
#endif
        HashBucket* hb = get_bucket(k);
        return (hb->get(k, outv));
    }

    bool remove(const HashKey& k, bool* removed, std::unique_ptr<HashNode>* outv) {
#ifdef GLOBAL_HASHSET_LOCK
        std::lock_guard<std::mutex> lk(m);
#endif
        HashBucket* hb = get_bucket(k);
        return (hb->remove(k, removed, outv));
    }

    // Iterate over all elements and make a callback to the node
    void foreach(std::function<void(const HashNode *)> callback) {
#ifdef GLOBAL_HASHSET_LOCK
        std::lock_guard<std::mutex> lk(m);
#endif
        for (auto i = 0U; i < m_nBuckets; i++) {
            m_buckets[i].foreach(callback);
        }
    }

  private:
    HashBucket* get_bucket(const HashKey& k) const { return &(m_buckets[k.get_hash_code() % m_nBuckets]); }

  private:
    uint32_t m_nBuckets;
    HashBucket* m_buckets;

#ifdef GLOBAL_HASHSET_LOCK
    std::mutex m;
#endif
};

} // namespace fds

#endif
