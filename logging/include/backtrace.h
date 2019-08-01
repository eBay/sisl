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

#define SIZE_T_UNUSED size_t __attribute__((unused))
#define VOID_UNUSED void __attribute__((unused))
#define UINT64_T_UNUSED uint64_t __attribute__((unused))
#define STR_UNUSED std::string __attribute__((unused))
#define INTPTR_UNUSED intptr_t __attribute__((unused))

#include <cstddef>
#include <sstream>
#include <string>

#include <cstdio>
#include <csignal>

extern "C" {
#include <cxxabi.h>
#include <execinfo.h>
#include <inttypes.h>
}

#ifdef __APPLE__
#include <mach-o/getsect.h>
#include <mach-o/dyld.h>

static UINT64_T_UNUSED static_base_address(void) {
    const struct segment_command_64* command = getsegbyname(SEG_TEXT /*"__TEXT"*/);
    uint64_t                         addr = command->vmaddr;
    return addr;
}

static STR_UNUSED get_exec_path() {
    char     path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0)
        return std::string();

    return path;
}

static STR_UNUSED get_file_part(const std::string& full_path) {
    size_t pos = full_path.rfind("/");
    if (pos == std::string::npos)
        return full_path;

    return full_path.substr(pos + 1, full_path.size() - pos - 1);
}

static INTPTR_UNUSED image_slide(void) {
    std::string exec_path = get_exec_path();
    if (exec_path.empty())
        return -1;

    auto image_count = _dyld_image_count();
    for (decltype(image_count) i = 0; i < image_count; ++i) {
        if (strcmp(_dyld_get_image_name(i), exec_path.c_str()) == 0) {
            return _dyld_get_image_vmaddr_slide(i);
        }
    }
    return -1;
}
#endif

#define _snprintf(msg, avail_len, cur_len, msg_len, args...)                                                           \
    avail_len = (avail_len > cur_len) ? (avail_len - cur_len) : 0;                                                     \
    msg_len = snprintf(msg + cur_len, avail_len, args);                                                                \
    cur_len += (avail_len > msg_len) ? msg_len : avail_len

static SIZE_T_UNUSED _stack_backtrace(void** stack_ptr, size_t stack_ptr_capacity) {
    return backtrace(stack_ptr, stack_ptr_capacity);
}

#if defined(__linux__)
static SIZE_T_UNUSED _stack_interpret_linux(void** stack_ptr, char** stack_msg, int stack_size, char* output_buf,
                                            size_t output_buflen);
#elif defined(__APPLE__)
static SIZE_T_UNUSED _stack_interpret_apple(void** stack_ptr, char** stack_msg, int stack_size, char* output_buf,
                                            size_t output_buflen);
#else
static SIZE_T_UNUSED _stack_interpret_other(void** stack_ptr, char** stack_msg, int stack_size, char* output_buf,
                                            size_t output_buflen);
#endif

static SIZE_T_UNUSED _stack_interpret(void** stack_ptr, int stack_size, char* output_buf, size_t output_buflen) {
    char** stack_msg = nullptr;
    stack_msg = backtrace_symbols(stack_ptr, stack_size);

    size_t len = 0;

#if defined(__linux__)
    len = _stack_interpret_linux(stack_ptr, stack_msg, stack_size, output_buf, output_buflen);

#elif defined(__APPLE__)
    len = _stack_interpret_apple(stack_ptr, stack_msg, stack_size, output_buf, output_buflen);

#else
    len = _stack_interpret_other(stack_ptr, stack_msg, stack_size, output_buf, output_buflen);

#endif
    free(stack_msg);

    return len;
}

#ifdef __linux__
static uintptr_t _extract_offset(const char* input_str, char* offset_str, int max_len) {
    uintptr_t actual_addr;

    int i = 0;
    while (input_str[i] != ')' && input_str[i] != 0x0) {
        ++i;
    }
    auto len = std::min(max_len - 1, i);
    sprintf(offset_str, "%.*s", len, input_str);

    // Convert hex string -> integer address.
    std::stringstream ss;
    ss << std::hex << offset_str;
    ss >> actual_addr;

    return actual_addr;
}

static bool _adjust_offset_symbol(const char* symbol_str, char* offset_str, uintptr_t* offset_ptr) {
    uintptr_t status = false;
    Dl_info   symbol_info;

    void* obj_file = dlopen(NULL, RTLD_LAZY);
    if (!obj_file) {
        return false;
    }

    void* addr = dlsym(obj_file, symbol_str);
    if (!addr) {
        goto done;
    }

    // extract the symbolic information pointed by address
    if (!dladdr(addr, &symbol_info)) {
        goto done;
    }
    *offset_ptr = ((uintptr_t)symbol_info.dli_saddr - ((uintptr_t)symbol_info.dli_fbase)) + *offset_ptr - 1;
    sprintf(offset_str, "%" PRIxPTR, *offset_ptr);
    status = true;

done:
    dlclose(obj_file);
    return status;
}

struct frame_info_t {
    char*     frame;         // Actual stack frame
    uint32_t  fname_len;     // The frame length containing the symbol to convert
    uintptr_t actual_addr;   // Actual address before converted to string
    char      addr_str[256]; // address to convert
    uint32_t  index;         // In a list of frames the index in it

    // Result section
    char* demangled_name;
    bool  demangler_alloced;

    char mangled_name[1024]; // Mangled name and file info
    char file_line[1024];
};

struct _addr2line_cmd_info {
    const char*                  this_frame_name;
    std::vector< frame_info_t* > single_invoke_finfos;
    std::string                  cmd;

    _addr2line_cmd_info(const char* frame, uint32_t fname_len) : this_frame_name(frame) {
        cmd = "addr2line -f -e ";
        cmd.append(frame, fname_len);
    }
};

static size_t find_frame_in_cmd_info(std::vector< _addr2line_cmd_info >& ainfos, char* frame, uint32_t fname_len) {
    for (auto i = 0u; i < ainfos.size(); ++i) {
        if (strncmp(ainfos[i].this_frame_name, frame, fname_len) == 0) {
            return i;
        }
    }
    return ainfos.size();
}

static void convert_frame_format(frame_info_t* finfos, size_t nframes) {
    std::vector< _addr2line_cmd_info > ainfos;
    for (auto f = 0u; f < nframes; ++f) {
        frame_info_t*        finfo = &finfos[f];
        _addr2line_cmd_info* ainfo;

        size_t ind = find_frame_in_cmd_info(ainfos, finfo->frame, finfo->fname_len);
        if (ind == ainfos.size()) {
            ainfos.emplace_back(finfo->frame, finfo->fname_len);
            ainfo = &ainfos.back();
        } else {
            ainfo = &ainfos[ind];
        }
        ainfo->single_invoke_finfos.push_back(finfo);
        ainfo->cmd.append(" ");
        ainfo->cmd.append(finfo->addr_str);
    }

    for (auto& ainfo : ainfos) {
        FILE* fp = popen(ainfo.cmd.c_str(), "r");
        if (!fp)
            continue;

        for (auto& finfop : ainfo.single_invoke_finfos) {
            int ret = fscanf(fp, "%1023s %1023s", finfop->mangled_name, finfop->file_line);
            (void)ret;

            int status;
            finfop->demangler_alloced = true;
            finfop->demangled_name = abi::__cxa_demangle(finfop->mangled_name, 0, 0, &status);
            if (finfop->demangled_name == nullptr) {
                std::string msg_str = finfop->frame;
                std::string _func_name = msg_str;
                size_t      s_pos = msg_str.find("(");
                size_t      e_pos = msg_str.rfind("+");
                if (e_pos == std::string::npos)
                    e_pos = msg_str.rfind(")");
                if (s_pos != std::string::npos && e_pos != std::string::npos) {
                    _func_name = msg_str.substr(s_pos + 1, e_pos - s_pos - 1);
                }
                if (_func_name.empty()) {
                    finfop->demangled_name = finfop->mangled_name;
                    finfop->demangler_alloced = false;
                } else {
                    finfop->demangled_name = (char*)malloc(strlen(_func_name.c_str()) + 1);
                    strcpy(finfop->demangled_name, _func_name.c_str());
                }
            }
        }
        pclose(fp);
    }
}

static SIZE_T_UNUSED _stack_interpret_linux(void** stack_ptr, char** stack_msg, int stack_size, char* output_buf,
                                            size_t output_buflen) {
    size_t                            cur_len = 0;
    size_t                            nframes = 0;
    std::unique_ptr< frame_info_t[] > finfos(new frame_info_t[stack_size]);

    // NOTE: starting from 1, skipping this frame.
    for (int i = 1; i < stack_size; ++i) {
        char* cur_frame = stack_msg[i];

        // `stack_msg[x]` format:
        //   /foo/bar/executable() [0xabcdef]
        //   /lib/x86_64-linux-gnu/libc.so.6(__libc_start_main+0xf0) [0x123456]

        // NOTE: with ASLR
        //   /foo/bar/executable(+0x5996) [0x555555559996]

        int fname_len = 0;
        while (cur_frame[fname_len] != '(' && cur_frame[fname_len] != ' ' && cur_frame[fname_len] != 0x0) {
            ++fname_len;
        }

        finfos[nframes].frame = stack_msg[i];
        finfos[nframes].fname_len = fname_len;
        finfos[nframes].index = nframes;

        uintptr_t actual_addr = 0x0;
        if (cur_frame[fname_len] == '(') {
            // Extract the symbol if present
            char _symbol[1024];
            int  _symbol_len = 0;
            int  _s = fname_len + 1;
            while (cur_frame[_s] != '+' && cur_frame[_s] != ')' && cur_frame[_s] != 0x0 && _symbol_len < 1023) {
                _symbol[_symbol_len++] = cur_frame[_s++];
            }
            _symbol[_symbol_len] = '\0';

            // Extract the offset
            if (cur_frame[_s] == '+') {
                // ASLR is enabled, get the offset from here.
                actual_addr = _extract_offset(&cur_frame[_s + 1], finfos[nframes].addr_str, 256);
            }

            // If symbol is present, try to add offset and get the correct addr_str
            if (_symbol_len > 0) {
                if (!_adjust_offset_symbol(_symbol, finfos[nframes].addr_str, &actual_addr)) {
                    // Resort to the default one
                    actual_addr = (uintptr_t)stack_ptr[i];
                    sprintf(finfos[nframes].addr_str, "%" PRIxPTR, actual_addr);
                }
            }
        } else {
            actual_addr = (uintptr_t)stack_ptr[i];
            sprintf(finfos[nframes].addr_str, "%" PRIxPTR, actual_addr);
        }

        finfos[nframes].actual_addr = actual_addr;
        ++nframes;
    }

    convert_frame_format(finfos.get(), nframes);
    for (size_t frame_num = 0u; frame_num < nframes; ++frame_num) {
        frame_info_t* finfo = &finfos[frame_num];

        size_t msg_len = 0;
        size_t avail_len = output_buflen;
        _snprintf(output_buf, avail_len, cur_len, msg_len, "#%-2zu 0x%016" PRIxPTR " in %s at %s\n", frame_num,
                  finfo->actual_addr, finfo->demangled_name, finfo->file_line);
        if (finfo->demangler_alloced) {
            free((void*)finfo->demangled_name);
        }
    }

    return cur_len;
}
#endif

static VOID_UNUSED skip_whitespace(const std::string base_str, size_t& cursor) {
    while (base_str[cursor] == ' ')
        ++cursor;
}

static VOID_UNUSED skip_glyph(const std::string base_str, size_t& cursor) {
    while (base_str[cursor] != ' ')
        ++cursor;
}

#ifdef __APPLE__
static SIZE_T_UNUSED _stack_interpret_apple(void** stack_ptr, char** stack_msg, int stack_size, char* output_buf,
                                            size_t output_buflen) {
    size_t cur_len = 0;

    size_t frame_num = 0;
    (void)frame_num;

    std::string exec_full_path = get_exec_path();
    std::string exec_file = get_file_part(exec_full_path);
    uint64_t    load_base = (uint64_t)image_slide() + static_base_address();

    // NOTE: starting from 1, skipping this frame.
    for (int i = 1; i < stack_size; ++i) {
        // `stack_msg[x]` format:
        //   8   foobar    0x000000010fd490da main + 1322
        if (!stack_msg[i] || stack_msg[i][0] == 0x0)
            continue;

        std::string base_str = stack_msg[i];

        size_t s_pos = 0;
        size_t len = 0;
        size_t cursor = 0;

        // Skip frame number part.
        skip_glyph(base_str, cursor);

        // Skip whitespace.
        skip_whitespace(base_str, cursor);
        s_pos = cursor;
        // Filename part.
        skip_glyph(base_str, cursor);
        len = cursor - s_pos;
        std::string filename = base_str.substr(s_pos, len);

        // Skip whitespace.
        skip_whitespace(base_str, cursor);
        s_pos = cursor;
        // Address part.
        skip_glyph(base_str, cursor);
        len = cursor - s_pos;
        std::string address = base_str.substr(s_pos, len);
        if (!address.empty() && address[0] == '?')
            continue;

        // Skip whitespace.
        skip_whitespace(base_str, cursor);
        s_pos = cursor;
        // Mangled function name part.
        skip_glyph(base_str, cursor);
        len = cursor - s_pos;
        std::string func_mangled = base_str.substr(s_pos, len);

        size_t msg_len = 0;
        size_t avail_len = output_buflen;

        _snprintf(output_buf, avail_len, cur_len, msg_len, "#%-2zu %s in ", frame_num++, address.c_str());

        if (filename != exec_file) {
            // Dynamic library.
            int   status;
            char* cc = abi::__cxa_demangle(func_mangled.c_str(), 0, 0, &status);
            if (cc) {
                _snprintf(output_buf, avail_len, cur_len, msg_len, "%s at %s\n", cc, filename.c_str());
            } else {
                _snprintf(output_buf, avail_len, cur_len, msg_len, "%s() at %s\n", func_mangled.c_str(),
                          filename.c_str());
            }
        } else {
            // atos return format:
            //   bbb(char) (in crash_example) (crash_example.cc:37)
            std::stringstream ss;
            ss << "atos -l 0x";
            ss << std::hex << load_base;
            ss << " -o " << exec_full_path;
            ss << " " << address;
            FILE* fp = popen(ss.str().c_str(), "r");
            if (!fp)
                continue;

            char atos_cstr[4096];
            fgets(atos_cstr, 4095, fp);

            std::string atos_str = atos_cstr;
            size_t      d_pos = atos_str.find(" (in ");
            if (d_pos == std::string::npos)
                continue;
            std::string function_part = atos_str.substr(0, d_pos);

            d_pos = atos_str.find(") (", d_pos);
            if (d_pos == std::string::npos)
                continue;
            std::string source_part = atos_str.substr(d_pos + 3);
            source_part = source_part.substr(0, source_part.size() - 2);

            _snprintf(output_buf, avail_len, cur_len, msg_len, "%s at %s\n", function_part.c_str(),
                      source_part.c_str());
        }
    }

    return cur_len;
}
#endif

static SIZE_T_UNUSED _stack_interpret_other(void** stack_ptr, char** stack_msg, int stack_size, char* output_buf,
                                            size_t output_buflen) {
    size_t cur_len = 0;
    size_t frame_num = 0;
    (void)frame_num;
    (void)stack_ptr;

    // NOTE: starting from 1, skipping this frame.
    for (int i = 1; i < stack_size; ++i) {
        // On non-Linux platform, just use the raw symbols.
        size_t msg_len = 0;
        size_t avail_len = output_buflen;
        _snprintf(output_buf, avail_len, cur_len, msg_len, "%s\n", stack_msg[i]);
    }
    return cur_len;
}

static SIZE_T_UNUSED stack_backtrace(char* output_buf, size_t output_buflen) {
    void* stack_ptr[256];
    int   stack_size = _stack_backtrace(stack_ptr, 256);
    return _stack_interpret(stack_ptr, stack_size, output_buf, output_buflen);
}

// LCOV_EXCL_STOP
