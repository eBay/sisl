/*
 * Created by Hari Kadayam on 14/02/18
 */

#pragma once

#include <memory>
#include <array>
#include <vector>

namespace sisl {
template < typename T, int32_t StaticCount >
class FlexArray {
public:
    FlexArray() : m_count(0) {}

    FlexArray(int32_t size) : FlexArray() { m_vec.reserve(size - StaticCount); }

    ~FlexArray() {
        auto c = m_count < StaticCount ? m_count : StaticCount;
        for (auto i = 0u; i < c; i++) {
            T* mem = (T*)&m_arr_mem[i * sizeof(T)];
            mem->~T();
        }
    }
    uint32_t push_back(T& value) {
        if (m_count < StaticCount) {
            get_in_array(m_count) = value;
        } else {
            m_vec.push_back(value);
        }
        m_count++;
        return m_count - 1;
    }

    template < class... Args >
    uint32_t emplace_back(Args&&... args) {
        if (m_count < StaticCount) {
            void* mem = (void*)&m_arr_mem[m_count * sizeof(T)];
            new (mem) T(std::forward< Args >(args)...);
        } else {
            m_vec.emplace_back(std::forward< Args >(args)...);
        }
        m_count++;
        return m_count - 1;
    }

    const T& operator[](uint32_t n) const {
        if (n < StaticCount) {
            return get_in_array(n);
        } else {
            return m_vec[n - StaticCount];
        }
    }

    T& operator[](uint32_t n) {
        if (n < StaticCount) {
            return get_in_array(n);
        } else {
            return m_vec[n - StaticCount];
        }
    }

    const T& back() const { return operator[](size() - 1); }
    T&       back() { return operator[](size() - 1); }
    const T& at(uint32_t n) const { return operator[](n); }
    T&       at(uint32_t n) { return operator[](n); }

    void reset() {
        m_count = 0;
        m_vec.clear();
    }

    size_t size() const { return m_count; }

private:
    T& get_in_array(uint32_t ind) const {
        T* arr = (T*)m_arr_mem;
        return arr[ind];
    }

private:
    uint32_t         m_count;
    uint8_t          m_arr_mem[sizeof(T) * StaticCount];
    std::vector< T > m_vec;
};

template < typename T, int32_t StaticCount >
class FlexArray< std::shared_ptr< T >, StaticCount > {
public:
    FlexArray() : m_count(0) {}

    uint32_t push_back(std::shared_ptr< T >& value) {
        if (m_count < StaticCount) {
            m_arr[m_count] = value;
        } else {
            m_vec.push_back(value);
        }
        m_count++;
        return m_count - 1;
    }

    template < class... Args >
    uint32_t emplace_back(Args&&... args) {
        if (m_count < StaticCount) {
            m_arr[m_count] = std::make_shared< T >(std::forward< Args >(args)...);
        } else {
            m_vec.emplace_back(std::make_shared< T >(std::forward< Args >(args)...));
        }
        m_count++;
        return m_count - 1;
    }

    std::shared_ptr< T > operator[](uint32_t n) {
        if (n < StaticCount) {
            return m_arr[n];
        } else {
            return m_vec[n - StaticCount];
        }
    }

    void freeup(uint32_t n) {
        if (n < StaticCount) {
            m_arr[n].reset();
        } else {
            m_vec[n - StaticCount].reset();
        }
    }

    std::shared_ptr< T > back() { return operator[](size() - 1); }
    std::shared_ptr< T > at(uint32_t n) { return operator[](n); }

    void reset() {
        m_count = 0;
        m_vec.clear();
    }

    size_t size() const { return m_count; }

private:
    uint32_t                                        m_count;
    std::array< std::shared_ptr< T >, StaticCount > m_arr;
    std::vector< std::shared_ptr< T > >             m_vec;
};

template < typename T, int32_t StaticCount >
class FlexArray< std::unique_ptr< T >, StaticCount > {
public:
    FlexArray() : m_count(0) {}

    uint32_t push_back(std::unique_ptr< T >&& value) {
        if (m_count < StaticCount) {
            m_arr[m_count] = std::move(value);
        } else {
            m_vec.push_back(std::move(value));
        }
        m_count++;
        return m_count - 1;
    }

    template < class... Args >
    uint32_t emplace_back(Args&&... args) {
        if (m_count < StaticCount) {
            m_arr[m_count] = std::make_unique< T >(std::forward< Args >(args)...);
        } else {
            m_vec.emplace_back(std::make_unique< T >(std::forward< Args >(args)...));
        }
        m_count++;
        return m_count - 1;
    }

    std::unique_ptr< T > release(uint32_t n) {
        if (n < StaticCount) {
            auto p = std::move(m_arr[n]);
            return p;
        } else {
            auto p = std::move(m_vec[n - StaticCount]);
            return std::move(p);
        }
    }

    void freeup(uint32_t n) {
        if (n < StaticCount) {
            m_arr[n].reset();
        } else {
            m_vec[n - StaticCount].reset();
        }
    }

    const T* operator[](uint32_t n) const {
        if (n < StaticCount) {
            return m_arr[n].get();
        } else {
            return m_vec[n - StaticCount].get();
        }
    }

    T* operator[](uint32_t n) {
        if (n < StaticCount) {
            return m_arr[n].get();
        } else {
            return m_vec[n - StaticCount].get();
        }
    }

    T* back() { return operator[](size() - 1); }
    T* at(uint32_t n) { return operator[](n); }

    void reset() {
        m_count = 0;
        m_vec.clear();
    }

    size_t size() const { return m_count; }

private:
    uint32_t                                        m_count;
    std::array< std::unique_ptr< T >, StaticCount > m_arr;
    std::vector< std::unique_ptr< T > >             m_vec;
};
} // namespace sisl
