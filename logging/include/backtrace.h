/**
 * Copyright (C) 2017-present Jung-Sang Ahn <jungsang.ahn@gmail.com>
 * All rights reserved.
 *
 * https://github.com/greensky00
 *
 * Stack Backtrace
 * Version: 0.3.5
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * ===========================================================
 *
 * Enhanced by hkadayam:
 *  -  While dlsym is available, backtrace does not provide symbol name, fixed it
 *     by calculating the offset through dlsym.
 */

#pragma once

// LCOV_EXCL_START

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>


#include <algorithm>

#include <cctype>
#include <cinttypes>

#include <cstdio>
#include <csignal>
#include <cstring>
#include <functional>


#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <cxxabi.h>
#include <execinfo.h>
#endif

#if defined(__linux__) 
#include <linux/limits.h>
#endif

#ifdef __APPLE__
#include <sys/syslimits.h>
#endif

namespace backtrace_detail {
constexpr size_t max_backtrace{256};
constexpr size_t process_name_length{PATH_MAX};
constexpr size_t symbol_name_length{1024};
constexpr size_t address_length{16};
} // namespace backtrace_detail


[[maybe_unused]] static size_t stack_backtrace_impl(void** const stack_ptr, const size_t stack_ptr_capacity) {
    return ::backtrace(stack_ptr, static_cast< int >(stack_ptr_capacity));
}

#if defined(__linux__)
[[maybe_unused]] extern size_t stack_interpret_linux_file(const void* const* const stack_ptr, FILE* const stack_file,
                                                          const size_t stack_size, char* const output_buf,
                                                          const size_t output_buflen, const bool trim_internal);
#elif defined(__APPLE__)
[[maybe_unused]] extern size_t stack_interpret_apple(const void* const* const stack_ptr,
                                                     const char* const* const stack_msg, const size_t stack_size,
                                                     char* const output_buf, const size_t output_buflen,
                                                     const bool trim_internal,
                                                     [[maybe_unused]] const bool trim_internal);
#else
[[maybe_unused]] extern size_t stack_interpret_other(const void* const* const stack_ptr,
                                                     const char* const* const stack_msg, const size_t stack_size,
                                                     char* const output_buf, const size_t output_buflen,
                                                     [[maybe_unused]] const bool trim_internal);
#endif

[[maybe_unused]] static size_t stack_interpret(void* const* const stack_ptr, const size_t stack_size,
                                               char* const output_buf, const size_t output_buflen,
                                               const bool trim_internal) {
#if defined(__linux__)
    std::unique_ptr< FILE, std::function< void(FILE* const) > > stack_file{std::tmpfile(), [](FILE* const fp) {
                                                                               if (fp)
                                                                                   std::fclose(fp);
                                                                           }};
    if (!stack_file)
        return 0;

    ::backtrace_symbols_fd(stack_ptr, static_cast< int >(stack_size), ::fileno(stack_file.get()));

    const size_t len{
        stack_interpret_linux_file(stack_ptr, stack_file.get(), stack_size, output_buf, output_buflen, trim_internal)};
#else
    const std::unique_ptr< char*, std::function< void(char** const) > > stack_msg{
        ::backtrace_symbols(stack_ptr, static_cast< int >(stack_size)),
        [](char** const ptr) { if (ptr) std::free(static_cast< void* >(ptr)); }};
#if defined(__APPLE__)
    const size_t len{
        stack_interpret_apple(stack_ptr, stack_msg.get(), stack_size, output_buf, output_buflen, trim_internal)};
#else
    const size_t len{
        stack_interpret_other(stack_ptr, stack_msg.get(), stack_size, output_buf, output_buflen, trim_internal)};
#endif
#endif
    return len;
}

[[maybe_unused]] static size_t stack_backtrace(char* const output_buf, const size_t output_buflen,
                                               const bool trim_internal) {
    // make this static so no memory allocation needed
    static std::mutex s_lock;
    static std::array< void*, backtrace_detail::max_backtrace > stack_ptr;
    {
        std::lock_guard< std::mutex > lock{s_lock};
        const size_t stack_size{stack_backtrace_impl(stack_ptr.data(), stack_ptr.size())};
        return stack_interpret(stack_ptr.data(), stack_size, output_buf, output_buflen, trim_internal);
    }
}

// LCOV_EXCL_STOP
