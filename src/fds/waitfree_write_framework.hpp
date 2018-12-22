//
// Created by Kadayam, Hari on 12/21/18.
//
#ifndef SISL_FDS_WAITFREE_WRITE_DS_HPP
#define SISL_FDS_WAITFREE_WRITE_DS_HPP

#include "utility/thread_buffer.hpp"
#include "utility/urcu_helper.hpp"
#include <tuple>

namespace sisl { namespace fds {

template <typename T, typename... Args>
class WrapperBuf {
public:
    template <class... Args1>
    WrapperBuf(Args1&&... args) :
            m_safe_buf(std::forward<Args1>(args)...),
            m_args(std::forward<Args1>(args)...) {}

    sisl::urcu_ptr< T > get_safe() { return m_safe_buf.get(); }
    T* rotate() {
        auto n = m_safe_buf.make_and_exchange(std::forward<Args>(m_args)...);
        return n->get();
    }

    std::unique_ptr< T > make_new() {
        return std::make_unique< T >(std::forward<Args>(m_args)...);
    }

private:
    sisl::urcu_data< T, Args... > m_safe_buf;
    std::tuple< Args... >         m_args;
};

/* This class implements a generic wait free writer framework to build a wait free writer structures on top of it.
 * The reader side is syncronized using locks and expected to perform very slow. However, writer side are wait free
 * using rcu and per thread buffer. Thus it can be typically used on structures which are very frequently updated but
 * rarely read (say metrics collection, list of garbage entries to cleanup etc)..
 */
template <typename T, typename... Args>
class WaitFreeWriterFramework {
public:
    template <class... Args1>
    WaitFreeWriterFramework(Args1&&... args) :
            m_buffer(std::forward<Args1>(args)...) {
    }

    T* writeable() {
        return m_buffer->get_safe().get();
    }

    std::unique_ptr< T > readable() {
        auto base = m_buffer->make_new();
        auto base_raw = base.get();
        m_buffer.access_all_threads([base_raw](WrapperBuf<T, Args...> *ptr) {
            auto old_ptr = ptr->rotate();
            T::merge(base_raw, old_ptr);
        });

        return std::move(base);
    }

private:
    sisl::ThreadBuffer< WrapperBuf< T, Args... > > m_buffer;
};
}} // namespace sisl::fds


#endif //SISL_FDS_WAITFREE_WRITE_DS_HPP
