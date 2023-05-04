/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Aditya Marella
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#pragma once

#include <chrono>
#include <thread>
#include <string>
#include <memory>

#ifdef _POSIX_THREADS
#include <pthread.h>
#endif

namespace sisl {

template < class F, class... Args >
std::thread thread_factory(const std::string name, F&& f, Args&&... args) {
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

template < class F, class... Args >
std::unique_ptr< std::thread > make_unique_thread(const std::string name, F&& f, Args&&... args) {
    return std::make_unique< std::thread >([=] {
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

template < class T >
void name_thread([[maybe_unused]] T& t, std::string const& name) {
#if defined(_POSIX_THREADS) && !defined(__APPLE__)
    auto ret = pthread_setname_np(t.native_handle(), name.substr(0, 15).c_str());
    if (ret != 0) LOGERROR("Set name of thread to {} failed ret={}", name, ret);
#else
    LOGINFO("No ability to set thread name: {}", name);
#endif /* _POSIX_THREADS */
}

template < class... Args >
auto named_thread(const std::string name, Args&&... args) {
    auto t = std::thread(std::forward< Args >(args)...);
    name_thread(t, name);
    return t;
}

template < class... Args >
auto named_jthread(const std::string name, Args&&... args) {
    auto j = std::jthread(std::forward< Args >(args)...);
    name_thread(j, name);
    return j;
}

} // namespace sisl
