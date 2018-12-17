//
// Created by Aditya, Marella on 12/29/17.
//

#pragma once

#include <thread>
#include <string>
#include <functional>
#include <memory>

#ifdef _POSIX_THREADS
#include <pthread.h>
#endif

namespace sisl {

template <class F, class... Args> std::thread thread_factory(const std::string name, F&& f, Args&&... args) {
    return std::thread([=] {
#ifdef _POSIX_THREADS
#ifdef __APPLE__
        pthread_setname_np(name.c_str());
#else
        pthread_setname_np(pthread_self(), name.c_str());
#endif /* __APPLE__ */
#endif /* _POSIX_THREADS */
        auto fun = std::mem_fn(f);
        fun(args...);
    });
}

template <class F, class... Args>
std::unique_ptr<std::thread> make_unique_thread(const std::string name, F&& f, Args&&... args) {
    return std::make_unique<std::thread>([=] {
#ifdef _POSIX_THREADS
#ifdef __APPLE__
        pthread_setname_np(name.c_str());
#else
        pthread_setname_np(pthread_self(), name.c_str());
#endif /* __APPLE__ */
#endif /* _POSIX_THREADS */
        auto fun = std::mem_fn(f);
        fun(args...);
    });
}

} // namespace sisl
