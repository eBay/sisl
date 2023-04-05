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

#include <thread>
#include <string>
#include <functional>
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

template < class... Args >
std::thread named_thread(const std::string name, Args&&... args) {
    auto t = std::thread(std::forward< Args >(args)...);

#ifdef _POSIX_THREADS
#ifndef __APPLE__
    auto tname = name.substr(0, 15);
    auto ret = pthread_setname_np(t.native_handle(), tname.c_str());
    if (ret != 0) { LOGERROR("Set name of thread to {} failed ret={}", tname, ret); }
#endif /* __APPLE__ */
#endif /* _POSIX_THREADS */

    return t;
}

} // namespace sisl
