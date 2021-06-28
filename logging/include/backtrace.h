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

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>

[[maybe_unused]] static uint64_t static_base_address(void) {
    const struct segment_command_64* const command{::getsegbyname(SEG_TEXT /*"__TEXT"*/)};
    const uint64_t addr{command->vmaddr};
    return addr;
}

[[maybe_unused]] static std::string get_exec_path() {
    std::array< char, 1024 > path;
    uint32_t size{static_cast< uint32_t >(path.size())};
    if (::_NSGetExecutablePath(path.data(), &size) != 0)
        return std::string{};

    return std::string{path.data()};
}

[[maybe_unused]] static std::string get_file_part(const std::string& full_path) {
    const size_t pos{full_path.rfind("/")};
    if (pos == std::string::npos)
        return full_path;

    return full_path.substr(pos + 1, full_path.size() - pos - 1);
}

[[maybe_unused]] static intptr_t image_slide(void) {
    const std::string exec_path{get_exec_path()};
    if (exec_path.empty())
        return -1;

    const auto image_count{::_dyld_image_count()};
    for (std::remove_const_t< decltype(image_count) > i{0}; i < image_count; ++i) {
        if (std::strcmp(::_dyld_get_image_name(i), exec_path.c_str()) == 0) {
            return ::_dyld_get_image_vmaddr_slide(i);
        }
    }
    return -1;
}
#endif

namespace backtrace_detail {
constexpr size_t max_backtrace{256};
}

template < typename... Args >
void _snprintf(char* const msg, size_t& avail_len, size_t& cur_len, size_t& msg_len, Args&&... args) {
    avail_len = (avail_len > cur_len) ? (avail_len - cur_len) : 0;
    msg_len = std::snprintf(msg + cur_len, avail_len, std::forward< Args >(args)...);
    cur_len += (avail_len > msg_len) ? msg_len : avail_len;
}

[[maybe_unused]] static size_t _stack_backtrace(void** const stack_ptr, const size_t stack_ptr_capacity) {
    return ::backtrace(stack_ptr, static_cast< int >(stack_ptr_capacity));
}

#if defined(__linux__)
[[maybe_unused]] static size_t _stack_interpret_linux(const void* const* const stack_ptr,
                                                      const char* const* const stack_msg, const size_t stack_size,
                                                      char* const output_buf, const size_t output_buflen,
                                                      const bool trim_internal);
#elif defined(__APPLE__)
[[maybe_unused]] static size_t _stack_interpret_apple(const void* const* const stack_ptr,
                                                      const char* const* const stack_msg, const size_t stack_size,
                                                      char* const output_buf, const size_t output_buflen,
                                                      const bool trim_internal,
                                                      [[maybe_unused]] const bool trim_internal);
#else
[[maybe_unused]] static size_t _stack_interpret_other(const void* const* const stack_ptr,
                                                      const char* const* const stack_msg, const size_t stack_size,
                                                      char* const output_buf, const size_t output_buflen,
                                                      [[maybe_unused]] const bool trim_internal);
#endif

[[maybe_unused]] static size_t _stack_interpret(void* const* const stack_ptr, const size_t stack_size,
                                                char* const output_buf, const size_t output_buflen,
                                                const bool trim_internal) {
    // NOTE:: possibly use file backed backtrace_symbols_fd
    const std::unique_ptr< char*, std::function< void(char** const) > > stack_msg{
        ::backtrace_symbols(stack_ptr, static_cast< int >(stack_size)),
        [](char** const ptr) { std::free(static_cast< void* >(ptr)); }};

#if defined(__linux__)
    const size_t len{
        _stack_interpret_linux(stack_ptr, stack_msg.get(), stack_size, output_buf, output_buflen, trim_internal)};

#elif defined(__APPLE__)
    const size_t len{
        _stack_interpret_apple(stack_ptr, stack_msg.get(), stack_size, output_buf, output_buflen, trim_internal)};

#else
    const size_t len{
        _stack_interpret_other(stack_ptr, stack_msg.get(), stack_size, output_buf, output_buflen, trim_internal)};

#endif

    return len;
}

#ifdef __linux__
static uintptr_t _extract_offset(const char* const input_str, char* const offset_str, const size_t max_len) {
    uintptr_t actual_addr{0};

    size_t i{0};
    while (input_str[i] != ')' && input_str[i] != 0x0) {
        ++i;
    }
    const auto len{std::min(max_len - 1, i)};
    std::snprintf(offset_str, max_len, "%.*s", static_cast< int >(len), input_str);

    // Convert hex string -> integer address.
    const char* pos{std::strpbrk(offset_str, "xX")};
    while (pos && (*(++pos) != '\0')) {
        const char c{*pos};
        uint8_t val{0};
        if ((c >= '0') && (c <= '9')) {
            val = static_cast< uint8_t >(c - '0');
        } else if ((c >= 'A') && (c <= 'F')) {
            val = static_cast< uint8_t >((c - 'A') + 10);
        } else if ((c >= 'a') && (c <= 'f')) {
            val = static_cast< uint8_t >((c - 'a') + 10);
        } else {
            break;
        }
        actual_addr <<= 4;
        actual_addr += val;
    }

    return actual_addr;
}

static bool _adjust_offset_symbol(const char* const symbol_str, char* const offset_str, const size_t max_len,
                                  uintptr_t* const offset_ptr) {
    bool status{false};
    Dl_info symbol_info;

    const std::unique_ptr< void, std::function< void(void* const) > > obj_file{::dlopen(nullptr, RTLD_LAZY),
                                                                               [](void* const ptr) { ::dlclose(ptr); }};
    if (!obj_file) {
        return status;
    }

    void* const addr{::dlsym(obj_file.get(), symbol_str)};
    if (!addr) {
        return status;
    }

    // extract the symbolic information pointed by address
    if (!::dladdr(addr, &symbol_info)) {
        return status;
    }
    *offset_ptr =
        (reinterpret_cast< uintptr_t >(symbol_info.dli_saddr) - reinterpret_cast< uintptr_t >(symbol_info.dli_fbase)) +
        *offset_ptr - 1;
    std::snprintf(offset_str, max_len, "%" PRIxPTR, *offset_ptr);
    status = true;

    return status;
}

struct frame_info_t {
    frame_info_t() : frame{nullptr}, fname_len{0}, actual_addr{0}, index{0} {
        addr_str.fill('\0');
        demangled_name.fill('\0');
        mangled_name.fill('\0');
        file_line.fill('\0');
    }

    const char* frame;                // Actual stack frame
    size_t fname_len;                 // The frame length containing the symbol to convert
    uintptr_t actual_addr;            // Actual address before converted to string
    std::array< char, 256 > addr_str; // address to convert
    uint32_t index;                   // In a list of frames the index in it

    // Result section
    std::array< char, 1024 > demangled_name;
    std::array< char, 1024 > mangled_name; // Mangled name and file info
    std::array< char, 1024 > file_line;
};

struct _addr2line_cmd_info {
    const char* this_frame_name;
    std::vector< frame_info_t* > single_invoke_finfos; // NOTE: maybe make array in future to avoid memory allocation
    std::array< char, 5000 > cmd;                      // 256*18 + buffer for process name
    size_t cmd_length;

    _addr2line_cmd_info() : this_frame_name{nullptr}, cmd_length{0} { cmd.fill('\0'); }

    _addr2line_cmd_info(const char* const frame, const size_t fname_len) : this_frame_name{frame}, cmd_length{0} {
        cmd.fill('\0');
        append_to_command("addr2line -f -e ");
        append_to_command(frame, fname_len);
    };

    void append_to_command(const char* const str, const size_t str_len = 0) {
        if (str_len == 0) {
            const auto written{std::snprintf(cmd.data() + cmd_length, cmd.size() - cmd_length, "%s", str)};
            if (cmd_length + written >= cmd.size() - 1) {
                cmd_length = cmd.size() - 1;
            } else {
                cmd_length += written;
            }
        } else {
            const size_t len{std::min< size_t >(cmd.size() - cmd_length - 1, str_len)};
            std::strncpy(cmd.data() + cmd_length, str, len);
            cmd_length += len;
        }
    };
};

static size_t find_frame_in_cmd_info(const std::vector< _addr2line_cmd_info >& ainfos, const char* const frame,
                                     const size_t fname_len) {
    size_t i{0};
    for (const auto& ainfo : ainfos) {
        if (std::strncmp(ainfo.this_frame_name, frame, fname_len) == 0) {
            return i;
        }
        ++i;
    }
    return ainfos.size();
}

static void convert_frame_format(frame_info_t* const finfos, const size_t nframes) {
    // make static to avoid memory allocation
    static std::vector< _addr2line_cmd_info > ainfos(backtrace_detail::max_backtrace);
    ainfos.clear();
    for (size_t f{0}; f < nframes; ++f) {
        frame_info_t* const finfo{&finfos[f]};
        _addr2line_cmd_info* ainfo;

        const size_t ind{find_frame_in_cmd_info(ainfos, finfo->frame, finfo->fname_len)};
        if (ind == ainfos.size()) {
            ainfos.emplace_back(finfo->frame, finfo->fname_len);
            ainfo = &ainfos.back();
        } else {
            ainfo = &ainfos[ind];
        }
        ainfo->single_invoke_finfos.push_back(finfo);
        ainfo->append_to_command(" ");
        ainfo->append_to_command(finfo->addr_str.data());
    }

    for (auto& ainfo : ainfos) {
        const std::unique_ptr< FILE, std::function< void(FILE* const) > > fp{::popen(ainfo.cmd.data(), "r"),
                                                                             [](FILE* const ptr) { ::pclose(ptr); }};
        if (!fp)
            continue;

        for (auto& finfop : ainfo.single_invoke_finfos) {
            static std::array< char, 15 > format;
            std::snprintf(format.data(), format.size(), "%%%zus %%%zus", finfop->mangled_name.size() - 1,
                          finfop->file_line.size() - 1);
            [[maybe_unused]] const int ret{
                std::fscanf(fp.get(), format.data(), finfop->mangled_name.data(), finfop->file_line.data())};

            int status;
            const std::unique_ptr< const char, std::function< void(const char* const) > > demangled_name{
                abi::__cxa_demangle(finfop->mangled_name.data(), 0, 0, &status),
                [](const char* const ptr) { std::free(static_cast< void* >(const_cast< char* >(ptr))); }};
            if (!demangled_name) {
                const char* const s_pos{std::strchr(finfop->frame, '(')};
                const char* e_pos{s_pos ? std::strrchr(finfop->frame, '+') : nullptr};
                if (s_pos && !e_pos)
                    e_pos = std::strrchr(finfop->frame, ')');
                if (!e_pos) {
                    std::strncpy(finfop->demangled_name.data(), finfop->mangled_name.data(),
                                 finfop->demangled_name.size());
                    finfop->demangled_name[finfop->demangled_name.size() - 1] = 0;

                } else {
                    const size_t len{std::min< size_t >(e_pos - s_pos - 1, finfop->demangled_name.size() - 1)};
                    std::strncpy(finfop->demangled_name.data(), s_pos + 1, len);
                }
            } else {
                std::strncpy(finfop->demangled_name.data(), demangled_name.get(), finfop->demangled_name.size() - 1);
            }
        }
    }
}

[[maybe_unused]] static size_t _stack_interpret_linux(const void* const* const stack_ptr,
                                                      const char* const* const stack_msg, const size_t stack_size,
                                                      char* const output_buf, const size_t output_buflen,
                                                      const bool trim_internal) {
    size_t cur_len{0};
    // make static to avoid memory allocation
    static std::vector< frame_info_t > finfos(backtrace_detail::max_backtrace);
    finfos.clear();

    // NOTE: starting from 1, skipping this frame.
    for (size_t i{1}; i < stack_size; ++i) {
        const char* const cur_frame{stack_msg[i]};

        // `stack_msg[x]` format:
        //   /foo/bar/executable() [0xabcdef]
        //   /lib/x86_64-linux-gnu/libc.so.6(__libc_start_main+0xf0) [0x123456]

        // NOTE: with ASLR
        //   /foo/bar/executable(+0x5996) [0x555555559996]

        size_t fname_len{0};
        while ((cur_frame[fname_len] != '(') && (cur_frame[fname_len] != ' ') && (cur_frame[fname_len] != 0x0)) {
            ++fname_len;
        }

        if (fname_len == 0)
            continue;

        finfos.emplace_back();
        finfos.back().frame = cur_frame;
        finfos.back().fname_len = fname_len;
        finfos.back().index = finfos.size() - 1;

        uintptr_t actual_addr{0x0};
        if (cur_frame[fname_len] == '(') {
            // Extract the symbol if present
            static std::array< char, 1024 > _symbol; // avoid memory allocation
            size_t _symbol_len{0};
            size_t _s{fname_len + 1};
            while ((cur_frame[_s] != 0x0) && (cur_frame[_s] != '+') && (cur_frame[_s] != ')') &&
                   (_symbol_len < _symbol.size() - 1)) {
                _symbol[_symbol_len++] = cur_frame[_s++];
            }
            _symbol[_symbol_len] = '\0';

            // Extract the offset
            if (cur_frame[_s] == '+') {
                // ASLR is enabled, get the offset from here.
                actual_addr =
                    _extract_offset(&cur_frame[_s + 1], finfos.back().addr_str.data(), finfos.back().addr_str.size());
            }

            // If symbol is present, try to add offset and get the correct addr_str
            if (_symbol_len > 0) {
                if (!_adjust_offset_symbol(_symbol.data(), finfos.back().addr_str.data(), finfos.back().addr_str.size(),
                                           &actual_addr)) {
                    // Resort to the default one
                    actual_addr = reinterpret_cast< uintptr_t >(stack_ptr[i]);
                    std::snprintf(finfos.back().addr_str.data(), finfos.back().addr_str.size(), "%" PRIxPTR,
                                  actual_addr);
                }
            }
        } else {
            actual_addr = reinterpret_cast< uintptr_t >(stack_ptr[i]);
            std::snprintf(finfos.back().addr_str.data(), finfos.back().addr_str.size(), "%" PRIxPTR, actual_addr);
        }

        finfos.back().actual_addr = actual_addr;
    }

    if (!finfos.empty()) {
        convert_frame_format(finfos.data(), finfos.size());
        // look for internal handler
        size_t frame_num{0};
        if (trim_internal) {
            for (auto& finfo : finfos) {
                if (std::strstr(finfo.demangled_name.data(), "sds_logging::bt_dumper") ||
                    std::strstr(finfo.demangled_name.data(), "sds_logging::crash_handler")) {
                    /*
                    // search to end for sigaction.c after handler found
                    size_t alt_frame_num{frame_num};
                    while (++alt_frame_num < finfos.size()) {
                        if (std::strstr(finfos[alt_frame_num].file_line.data(), "sigaction.c")) {
                            // advance to right after sigaction.c
                            frame_num = alt_frame_num + 1;
                            break;
                        }
                    }
                    */
                    break;
                }
                ++frame_num;
            }
            if (frame_num == finfos.size())
                frame_num = 0;
        }
        size_t line_num{0};
        for (size_t frame{frame_num}; frame < finfos.size(); ++frame, ++line_num) {
            size_t msg_len{0};
            size_t avail_len{output_buflen};
            _snprintf(output_buf, avail_len, cur_len, msg_len, "#%-2zu 0x%016" PRIxPTR " in %s at %s\n", line_num,
                      finfos[frame].actual_addr, finfos[frame].demangled_name.data(), finfos[frame].file_line.data());
            ++frame_num;
        }
    }

    return cur_len;
}
#endif

[[maybe_unused]] static void skip_whitespace(const std::string& base_str, size_t& cursor) {
    while ((cursor < base_str.size()) && (base_str[cursor] == ' '))
        ++cursor;
}

[[maybe_unused]] static void skip_glyph(const std::string& base_str, size_t& cursor) {
    while ((cursor < base_str.size()) && (base_str[cursor] != ' '))
        ++cursor;
}

#ifdef __APPLE__
[[maybe_unused]] static size_t _stack_interpret_apple([[maybe_unused]] const void* const* const stack_ptr,
                                                      const char* const* const stack_msg, const size_t stack_size,
                                                      char* const output_buf, const size_t output_buflen, ,
                                                      [[maybe_unused]] const bool trim_internal) {
    size_t cur_len{0};

    [[maybe_unused]] size_t frame_num{0};

    const std::string exec_full_path{get_exec_path()};
    const std::string exec_file{get_file_part(exec_full_path)};
    const uint64_t load_base{static_cast< uint64_t >(image_slide()) + static_base_address()};

    // NOTE: starting from 1, skipping this frame.
    for (size_t i{1}; i < stack_size; ++i) {
        // `stack_msg[x]` format:
        //   8   foobar    0x000000010fd490da main + 1322
        if (!stack_msg[i] || (stack_msg[i][0] == 0x0))
            continue;

        const std::string base_str{stack_msg[i]};

        size_t s_pos{0};
        size_t len{0};
        size_t cursor{0};

        // Skip frame number part.
        skip_glyph(base_str, cursor);

        // Skip whitespace.
        skip_whitespace(base_str, cursor);
        s_pos = cursor;
        // Filename part.
        skip_glyph(base_str, cursor);
        len = cursor - s_pos;
        const std::string filename{base_str.substr(s_pos, len)};

        // Skip whitespace.
        skip_whitespace(base_str, cursor);
        s_pos = cursor;
        // Address part.
        skip_glyph(base_str, cursor);
        len = cursor - s_pos;
        const std::string address{base_str.substr(s_pos, len)};
        if (!address.empty() && address[0] == '?')
            continue;

        // Skip whitespace.
        skip_whitespace(base_str, cursor);
        s_pos = cursor;
        // Mangled function name part.
        skip_glyph(base_str, cursor);
        len = cursor - s_pos;
        const std::string func_mangled{base_str.substr(s_pos, len)};

        size_t msg_len = 0;
        size_t avail_len = output_buflen;

        _snprintf(output_buf, avail_len, cur_len, msg_len, "#%-2zu %s in ", frame_num++, address.c_str());

        if (filename != exec_file) {
            // Dynamic library.
            int status;
            const std::unique_ptr< const char, std::function< void(const char* const) > > cc{
                abi::__cxa_demangle(func_mangled.c_str(), 0, 0, &status),
                [](const char* const ptr) { std::free(static_cast< void* >(const_cast< char* >(ptr))); }};
            if (cc) {
                _snprintf(output_buf, avail_len, cur_len, msg_len, "%s at %s\n", cc.get(), filename.c_str());
            } else {
                _snprintf(output_buf, avail_len, cur_len, msg_len, "%s() at %s\n", func_mangled.c_str(),
                          filename.c_str());
            }
        } else {
            // atos return format:
            //   bbb(char) (in crash_example) (crash_example.cc:37)
            std::ostringstream ss;
            ss << "atos -l 0x";
            ss << std::hex << load_base;
            ss << " -o " << exec_full_path;
            ss << " " << address;
            const std::unique_ptr< FILE, std::function< void(FILE* const) > > fp{
                ::popen(ss.str().c_str() "r"), [](FILE* const ptr) { ::pclose(ptr); }};
            if (!fp)
                continue;

            std::array< char, 4096 > atos_cstr;
            std::fgets(atos_cstr.data(), atos_cstr.size() - 1, fp);

            const std::string atos_str{atos_cstr.data()};
            size_t d_pos{atos_str.find(" (in ")};
            if (d_pos == std::string::npos)
                continue;
            const std::string function_part{atos_str.substr(0, d_pos)};

            d_pos = atos_str.find(") (", d_pos);
            if (d_pos == std::string::npos)
                continue;
            std::string source_part{atos_str.substr(d_pos + 3)};
            source_part = source_part.substr(0, source_part.size() - 2);

            _snprintf(output_buf, avail_len, cur_len, msg_len, "%s at %s\n", function_part.c_str(),
                      source_part.c_str());
        }
    }

    return cur_len;
}
#endif

[[maybe_unused]] static size_t _stack_interpret_other([[maybe_unused]] const void* const* const stack_ptr,
                                                      const char* const* const stack_msg, const size_t stack_size,
                                                      char* const output_buf, const size_t output_buflen,
                                                      [[maybe_unused]] const bool trim_internal) {
    size_t cur_len{0};
    [[maybe_unused]] size_t frame_num{0};

    // NOTE: starting from 1, skipping this frame.
    for (size_t i{1}; i < stack_size; ++i) {
        // On non-Linux platform, just use the raw symbols.
        size_t msg_len{0};
        size_t avail_len{output_buflen};
        _snprintf(output_buf, avail_len, cur_len, msg_len, "%s\n", stack_msg[i]);
    }
    return cur_len;
}

[[maybe_unused]] static size_t stack_backtrace(char* const output_buf, const size_t output_buflen,
                                               const bool trim_internal) {
    // make this static so no memory allocation needed
    static std::mutex s_lock;
    static std::array< void*, backtrace_detail::max_backtrace > stack_ptr;
    {
        std::lock_guard<std::mutex> lock{s_lock};
        const size_t stack_size{_stack_backtrace(stack_ptr.data(), stack_ptr.size())};
        return _stack_interpret(stack_ptr.data(), stack_size, output_buf, output_buflen, trim_internal);
    }
}

// LCOV_EXCL_STOP
