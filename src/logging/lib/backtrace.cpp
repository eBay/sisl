/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam, Bryan Zimmerman
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on  * an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#include <sys/select.h>
#endif

#ifdef __linux__
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#endif

#include "logging.h"

#include "backtrace.h"

namespace {

#ifdef __APPLE__
[[maybe_unused]] uint64_t static_base_address(void) {
    const struct segment_command_64* const command{::getsegbyname(SEG_TEXT /*"__TEXT"*/)};
    const uint64_t addr{command->vmaddr};
    return addr;
}

[[maybe_unused]] std::string get_exec_path() {
    std::array< char, 1024 > path;
    uint32_t size{static_cast< uint32_t >(path.size())};
    if (::_NSGetExecutablePath(path.data(), &size) != 0) return std::string{};

    return std::string{path.data()};
}

[[maybe_unused]] std::string get_file_part(const std::string& full_path) {
    const size_t pos{full_path.rfind("/")};
    if (pos == std::string::npos) return full_path;

    return full_path.substr(pos + 1, full_path.size() - pos - 1);
}

[[maybe_unused]] intptr_t image_slide(void) {
    const std::string exec_path{get_exec_path()};
    if (exec_path.empty()) return -1;

    const auto image_count{::_dyld_image_count()};
    for (std::remove_const_t< decltype(image_count) > i{0}; i < image_count; ++i) {
        if (std::strcmp(::_dyld_get_image_name(i), exec_path.c_str()) == 0) {
            return ::_dyld_get_image_vmaddr_slide(i);
        }
    }
    return -1;
}
#endif // __APPLE__

template < typename... Args >
[[maybe_unused]] void t_snprintf(char* const msg, size_t& avail_len, size_t& cur_len, size_t& msg_len, Args&&... args) {
    avail_len = (avail_len > cur_len) ? (avail_len - cur_len) : 0;
    if (avail_len > 0) {
        msg_len = std::snprintf(msg + cur_len, avail_len, std::forward< Args >(args)...);
        cur_len += (avail_len > msg_len) ? msg_len : avail_len;
    }
}

[[maybe_unused]] uintptr_t convert_hex_to_integer(const char* const input_str) {
    uintptr_t actual_addr{0};

    // Convert hex string -> integer address.
    const char* pos{std::strpbrk(input_str, "xX")};
    if (!pos) return actual_addr;

    while (++pos && (*pos != 0x00)) {
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

// trim whitespace of the given null terminated string of length input_length not including null terminator
[[maybe_unused]] size_t trim_whitespace(char* const input, const size_t input_length) {
    size_t length{input_length};
    if (length == 0) return length;

    // trim beginning
    size_t trim{0};
    while (trim < length) {
        if (std::isspace(input[trim]) != 0)
            ++trim;
        else
            break;
    }
    if (trim > 0) {
        length -= trim;
        std::memmove(&input[0], &input[trim], length + 1); // include null terminator
    }

    // trim end
    while (length > 0) {
        if (std::isspace(input[length - 1]) != 0) {
            input[length - 1] = 0x00;
            --length;
        } else
            break;
    }
    return length;
}

[[maybe_unused]] void skip_whitespace(const std::string& base_str, size_t& cursor) {
    while ((cursor < base_str.size()) && (base_str[cursor] == ' '))
        ++cursor;
}

[[maybe_unused]] void skip_glyph(const std::string& base_str, size_t& cursor) {
    while ((cursor < base_str.size()) && (base_str[cursor] != ' '))
        ++cursor;
}

template < typename... Args >
[[maybe_unused]] void log_message(const char* const format, Args&&... args) {
    auto& logger{sisl::logging::GetLogger()};
    auto& critical_logger{sisl::logging::GetCriticalLogger()};

    if (logger) { logger->critical(format, std::forward< Args >(args)...); }
    if (critical_logger) { critical_logger->critical(format, std::forward< Args >(args)...); }
}

#ifdef __linux__
std::pair< bool, uintptr_t > offset_symbol_address(const char* const file_name, const char* const symbol_str,
                                                   const uintptr_t symbol_address) {
    bool status{false};
    uintptr_t offset_address{symbol_address};
    Dl_info symbol_info;

    void* addr{nullptr};
    {
        const std::unique_ptr< void, std::function< void(void* const) > > obj_file{::dlopen(file_name, RTLD_LAZY),
                                                                                   [](void* const ptr) {
                                                                                       if (ptr) ::dlclose(ptr);
                                                                                   }};
        if (!obj_file) { return {status, offset_address}; }

        addr = ::dlsym(obj_file.get(), symbol_str);
        if (!addr) { return {status, offset_address}; }
    }

    // extract the symbolic information pointed by address
    if (!::dladdr(addr, &symbol_info)) { return {status, offset_address}; }
    offset_address +=
        (reinterpret_cast< uintptr_t >(symbol_info.dli_saddr) - reinterpret_cast< uintptr_t >(symbol_info.dli_fbase)) -
        1;
    status = true;

    return {status, offset_address};
}

std::pair< const char*, const char* > convert_symbol_line(const char* const file_name, const size_t file_name_length,
                                                          const uintptr_t address, const char* const symbol_name) {
    static constexpr size_t line_number_length{24};
    static constexpr std::array< char, 10 > s_pipe_unknown{"??\0??:?\0"};
    const char* mangled_name{s_pipe_unknown.data()};
    size_t mangled_name_length{2};
    const char* file_line{s_pipe_unknown.data() + 3};
    size_t file_line_length{4};

    if (file_name_length == 0) return {mangled_name, file_line};

    // form the command
    static constexpr size_t extra_length{
        10}; // includes single quotes around process name and " -a 0x" and null terminator
    static constexpr std::array< char, 18 > prefix{"addr2line -f -e \'"};
    static std::array<
        char, extra_length + prefix.size() + backtrace_detail::file_name_length + backtrace_detail::address_length >
        s_command;
    size_t command_length{prefix.size() - 1};
    std::memcpy(s_command.data(), prefix.data(), command_length);
    std::memcpy(s_command.data() + command_length, file_name, file_name_length);
    command_length += file_name_length;
    static std::array< char, backtrace_detail::address_length + 1 > s_address;
    std::snprintf(s_address.data(), s_address.size(), "%" PRIxPTR, address);
    std::snprintf(s_command.data() + command_length, s_command.size() - command_length, "\' -a 0x%s", s_address.data());
    // log_message("SISL Logging - symbol_line with command {}", s_command.data());

    // execute command and read data from pipe
    {
        const std::unique_ptr< FILE, std::function< void(FILE* const) > > fp{::popen(s_command.data(), "re"),
                                                                             [](FILE* const ptr) {
                                                                                 if (ptr) ::pclose(ptr);
                                                                             }};
        if (fp) {
            // wait on pipe
            const auto waitOnPipe{[rfd{::fileno(fp.get())}](const uint64_t wait_ms) {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(rfd, &rfds);

                timespec ts;
                ts.tv_sec = static_cast< decltype(ts.tv_sec) >(wait_ms / 1000);
                ts.tv_nsec = static_cast< decltype(ts.tv_nsec) >((wait_ms % 1000) * 1000000);
                const int result{::pselect(FD_SETSIZE, &rfds, nullptr, nullptr, &ts, nullptr)};
                return (result > 0);
            }};

            // read the pipe
            constexpr uint64_t loop_wait_ms{1000};
            constexpr size_t read_tries{static_cast< size_t >(backtrace_detail::pipe_timeout_ms / loop_wait_ms)};
            constexpr size_t newlines_expected{3};
            std::array< const char*, newlines_expected > newline_positions;
            size_t total_bytes_read{0};
            size_t total_newlines{0};
            static std::array<
                char, backtrace_detail::symbol_name_length + backtrace_detail::file_name_length + line_number_length >
                s_pipe_data;
            bool address_found{false};
            for (size_t read_try{0}; (read_try < read_tries) && (total_newlines < newlines_expected); ++read_try) {
                if (waitOnPipe(loop_wait_ms)) {
                    size_t bytes{std::fread(s_pipe_data.data() + total_bytes_read, 1,
                                            s_pipe_data.size() - total_bytes_read, fp.get())};
                    // count new newlines and null terminate at those positions
                    for (size_t byte_num{0}; byte_num < bytes; ++byte_num) {
                        const auto updateNewlines{[&total_newlines, &newline_positions](const size_t offset) {
                            if (total_newlines < newlines_expected) {
                                newline_positions[total_newlines] = &s_pipe_data[offset];
                            }
                            ++total_newlines;
                        }};

                        const size_t offset{byte_num + total_bytes_read};
                        if (s_pipe_data[offset] == '\n') {
                            s_pipe_data[offset] = 0x00; // convert newline to null terminator
                            if (!address_found) {
                                // check for address in pipe data
                                const char* const address_ptr{std::strstr(s_pipe_data.data(), s_address.data())};
                                if (address_ptr) {
                                    address_found = true;
                                    updateNewlines(offset);
                                } else {
                                    // wipe all pipe data up to and including null ptr
                                    if (byte_num < bytes - 1) {
                                        std::memmove(s_pipe_data.data(), s_pipe_data.data() + offset + 1,
                                                     bytes + total_bytes_read - offset - 1);
                                        bytes -= byte_num + 1;
                                    } else {
                                        bytes = 0;
                                    }
                                    total_bytes_read = 0;
                                    byte_num = 0;
                                }
                            } else {
                                updateNewlines(offset);
                            }
                        }
                    }
                    total_bytes_read += bytes;
                }
            }
            s_pipe_data[total_bytes_read] = 0;

            // read the pipe
            if (total_newlines > 0) {
                if (total_newlines == 3) {
                    // file and name info
                    file_line = newline_positions[1] + 1;
                    file_line_length = static_cast< size_t >(newline_positions[2] - file_line);
                    file_line_length = trim_whitespace(const_cast< char* >(file_line), file_line_length);
                    mangled_name = newline_positions[0] + 1;
                    mangled_name_length = static_cast< size_t >(newline_positions[1] - mangled_name);
                    mangled_name_length = trim_whitespace(const_cast< char* >(mangled_name), mangled_name_length);
                } else if (total_newlines == 2) {
                    log_message("SISL Logging - Pipe did not return expected number of newlines {}", total_newlines);
                    mangled_name = newline_positions[0] + 1;
                    mangled_name_length = static_cast< size_t >(newline_positions[1] - mangled_name);
                    mangled_name_length = trim_whitespace(const_cast< char* >(mangled_name), mangled_name_length);
                } else {
                    log_message("SISL Logging - Pipe did not return expected number of newlines {}", total_newlines);
                }
            } else {
                // no pipe data just continue
                log_message("SISL Logging - No pipe data");
            }
        } else {
            // no pipe just continue
            log_message("SISL Logging - Could not open pipe to resolve symbol_line with command {}", s_command.data());
        }
        if (std::strstr(mangled_name, "??")) {
            log_message("SISL Logging - Could not resolve symbol_line with command {}", s_command.data());
        }
    }

    // demangle the name
    static std::array< char, backtrace_detail::symbol_name_length > demangled_name;
    {
        [[maybe_unused]] int status{-3}; // one of the arguments is invalid
        const std::unique_ptr< const char, std::function< void(const char* const) > > cxa_demangled_name{
            std::strstr(mangled_name, "??") ? nullptr : abi::__cxa_demangle(mangled_name, 0, 0, &status),
            [](const char* const ptr) {
                if (ptr) std::free(static_cast< void* >(const_cast< char* >(ptr)));
            }};
        if (!cxa_demangled_name) {
            if (status != -2) { // check that not a mangled name
                log_message("SISL Logging - Could not demangle name {} error {}", mangled_name, status);
            }
            if (!symbol_name || (symbol_name[0] == '+') || (symbol_name[0] == 0x00)) {
                // no symbol name so use mangled name
                std::memcpy(demangled_name.data(), mangled_name, mangled_name_length);
                demangled_name[mangled_name_length] = 0x00;
            } else {
                // use the symbol name
                std::snprintf(demangled_name.data(), demangled_name.size(), "%s", symbol_name);
            }
        } else {
            // use the demangled name
            std::snprintf(demangled_name.data(), demangled_name.size(), "%s", cxa_demangled_name.get());
        }
    }

    // resolve file name absolute path
    static std::array< char, backtrace_detail::file_name_length + line_number_length > s_absolute_file_path;
    static std::array< char, backtrace_detail::file_name_length > s_relative_file_path;
    const char* const colon_ptr{std::strrchr(file_line, ':')};
    const size_t relative_file_name_length{colon_ptr ? static_cast< size_t >(colon_ptr - file_line) : file_line_length};
    if (std::strstr(file_line, "??") || (relative_file_name_length == 0)) {
        // no resolved file name, use process/lib name
        std::memcpy(s_relative_file_path.data(), file_name, file_name_length);
        s_relative_file_path[file_name_length] = 0x00;
    } else {
        // use previoulsy received possibly relative path
        std::memcpy(s_relative_file_path.data(), file_line, relative_file_name_length);
        s_relative_file_path[relative_file_name_length] = 0x00;
    }
    if (const char* const path{::realpath(s_relative_file_path.data(), s_absolute_file_path.data())}) {
        // absolute path resolved
    } else {
        // use the relative file name path
        std::strcpy(s_absolute_file_path.data(), s_relative_file_path.data());
    }
    // append line number
    if (colon_ptr) {
        std::strcat(s_absolute_file_path.data(), colon_ptr);
    } else {
        std::strcat(s_absolute_file_path.data(), ":?");
    }

    return {demangled_name.data(), s_absolute_file_path.data()};
}

#endif // __linux__

} // anonymous namespace

#ifdef __linux__

const char* linux_process_name() {
    static std::array< char, backtrace_detail::file_name_length > s_process_name;
    const auto length{::readlink("/proc/self/exe", s_process_name.data(), s_process_name.size())};
    if (length == -1) {
        s_process_name[0] = 0;
    } else if (static_cast< size_t >(length) == s_process_name.size()) {
        // truncation occurred so null terminate
        s_process_name[s_process_name.size() - 1] = 0;
    } else {
        // success so null terminate
        s_process_name[static_cast< size_t >(length)] = 0;
    }
    return s_process_name.data();
}

size_t stack_interpret_linux_file(const void* const* const stack_ptr, FILE* const stack_file, const size_t stack_size,
                                  char* const output_buf, const size_t output_buflen, const bool trim_internal) {
    std::rewind(stack_file);
    char c{0x00};

    /*
    while (!feof(stack_file)) std::putc(fgetc(stack_file), stdout);
    std::rewind(stack_file);
    */

    // get the current process name
    const char* const absolute_process_name{linux_process_name()};
    const char* const slash_pos{std::strrchr(absolute_process_name, '/')};
    const char* const process_name{slash_pos ? slash_pos + 1 : absolute_process_name};
    const size_t process_name_length{std::strlen(process_name)};

    static std::array< size_t, backtrace_detail::max_backtrace > s_output_line_start;
    size_t cur_len{0};
    size_t chars_read{0};
    const auto extractName{[&stack_file, &c, &chars_read](auto& dest, const auto& term_chars) {
        size_t len{0};
        const auto nullTerminate{[&len, &dest]() {
            if (len < dest.size()) {
                dest[len] = 0x00;
            } else {
                dest[dest.size() - 1] = 0x00;
            }
            return std::min(len, dest.size() - 1);
        }};
        while (!std::feof(stack_file)) {
            c = static_cast< char >(std::fgetc(stack_file));
            if (!std::feof(stack_file)) {
                ++chars_read;
                if (std::find(std::cbegin(term_chars), std::cend(term_chars), c) != std::cend(term_chars)) {
                    return nullTerminate();
                } else if (len < dest.size()) {
                    dest[len] = c;
                }
                ++len;
            }
        }
        return nullTerminate();
    }};

    // read till end of line
    const auto readTillEOL{[&stack_file, &c, &chars_read]() {
        while (!std::feof(stack_file)) {
            c = static_cast< char >(std::fgetc(stack_file));
            if (!std::feof(stack_file)) {
                ++chars_read;
                if (c == '\n') return;
            }
        }
        return;
    }};

    size_t trim_line{0};
    size_t line_num{0};
    size_t msg_len{0};
    size_t avail_len{output_buflen};
    // NOTE: starting from 1, skipping this line.
    readTillEOL();
    for (size_t i{1}; (i < stack_size) && !std::feof(stack_file); ++i) {
        // `stack_msg[x]` format:
        //   /foo/bar/executable() [0xabcdef]
        //   /foo/bar/executable()(+0xf0) [0x123456]
        //   /lib/x86_64-linux-gnu/libc.so.6(__libc_start_main+0xf0) [0x123456]

        // NOTE: with ASLR
        //   /foo/bar/executable(+0x5996) [0x555555559996]

        static std::array< char, backtrace_detail::file_name_length > s_file_name;
        const size_t file_name_length{
            trim_whitespace(s_file_name.data(), extractName(s_file_name, std::array< char, 2 >{'(', '\n'}))};

        if (file_name_length == 0) {
            if (c != '\n') readTillEOL();
            continue;
        }

        uintptr_t actual_addr{reinterpret_cast< uintptr_t >(stack_ptr[i])};
        static std::array< char, backtrace_detail::symbol_name_length > s_symbol;
        s_symbol[0] = 0x00;
        if (c == '(') {
            // Extract the symbol if present
            const size_t symbol_len{
                trim_whitespace(s_symbol.data(), extractName(s_symbol, std::array< char, 2 >{')', '\n'}))};

            // Extract the offset
            if (symbol_len > 0) {
                char* const plus{std::strchr(s_symbol.data(), '+')};
                const uintptr_t symbol_address{plus ? convert_hex_to_integer(plus + 1) : 0};

                if (plus == s_symbol.data()) {
                    // ASLR is enabled, get the offset from here.
                    actual_addr = symbol_address;
                } else {
                    if (plus) {
                        // truncate symbol at + so just function name
                        *plus = 0x00;
                    }
                    const bool main_program{
                        file_name_length < process_name_length
                            ? false
                            : std::strcmp(process_name, s_file_name.data() + file_name_length - process_name_length) ==
                                0};
                    const auto [offset_result, offset_addr]{offset_symbol_address(
                        main_program ? nullptr : s_file_name.data(), s_symbol.data(), symbol_address)};
                    if (offset_result) {
                        actual_addr = offset_addr;
                    } else {
                        log_message(
                            "SISL Logging - Could not resolve offset_symbol_address for symbol {} with address {}",
                            s_symbol.data(), symbol_address);
                    }
                }
            }
        }

        const auto [demangled_name,
                    file_line]{convert_symbol_line(s_file_name.data(), file_name_length, actual_addr, s_symbol.data())};
        if (!demangled_name || !file_line) {
            if (c != '\n') readTillEOL();
            continue;
        }

        if (trim_internal) {
            if (std::strstr(demangled_name, "sisl::logging::bt_dumper") ||
                std::strstr(demangled_name, "sisl::logging::crash_handler")) {
                trim_line = line_num;
            }
        }
        s_output_line_start[line_num] = cur_len;
        t_snprintf(output_buf, avail_len, cur_len, msg_len, "#%-3zu 0x%016" PRIxPTR " in %s at %s\n", line_num,
                   actual_addr, demangled_name, file_line);
        ++line_num;

        if (c != '\n') readTillEOL();
    }

    if (trim_line > 0) {
        // trim characters and include null character at end
        const size_t offset{s_output_line_start[trim_line]};
        cur_len -= offset;
        std::memmove(output_buf, output_buf + offset, cur_len + 1); // move terminating null

        // renumber lines
        for (size_t current_line{0}; current_line < line_num - trim_line; ++current_line) {
            std::array< char, 5 > line_str;
            const int length{std::snprintf(line_str.data(), line_str.size(), "#%-3zu", current_line)};
            std::memcpy(output_buf + s_output_line_start[trim_line + current_line] - offset, line_str.data(), length);
        }
    }

    return cur_len;
}
#endif // __linux__

#ifdef __APPLE__
size_t stack_interpret_apple([[maybe_unused]] const void* const* const stack_ptr, const char* const* const stack_msg,
                             const size_t stack_size, char* const output_buf, const size_t output_buflen, ,
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
        if (!stack_msg[i] || (stack_msg[i][0] == 0x0)) continue;

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
        if (!address.empty() && address[0] == '?') continue;

        // Skip whitespace.
        skip_whitespace(base_str, cursor);
        s_pos = cursor;
        // Mangled function name part.
        skip_glyph(base_str, cursor);
        len = cursor - s_pos;
        const std::string func_mangled{base_str.substr(s_pos, len)};

        size_t msg_len{0};
        size_t avail_len = output_buflen;

        t_snprintf(output_buf, avail_len, cur_len, msg_len, "#%-3zu %s in ", frame_num++, address.c_str());

        if (filename != exec_file) {
            // Dynamic library.
            int status;
            const std::unique_ptr< const char, std::function< void(const char* const) > > cc{
                abi::__cxa_demangle(func_mangled.c_str(), 0, 0, &status), [](const char* const ptr) {
                    if (ptr) std::free(static_cast< void* >(const_cast< char* >(ptr)));
                }};
            if (cc) {
                t_snprintf(output_buf, avail_len, cur_len, msg_len, "%s at %s\n", cc.get(), filename.c_str());
            } else {
                t_snprintf(output_buf, avail_len, cur_len, msg_len, "%s() at %s\n", func_mangled.c_str(),
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
            const std::unique_ptr< FILE, std::function< void(FILE* const) > > fp{::popen(ss.str().c_str() "r"),
                                                                                 [](FILE* const ptr) {
                                                                                     if (ptr) ::pclose(ptr);
                                                                                 }};
            if (!fp) continue;

            std::array< char, 4096 > atos_cstr;
            std::fgets(atos_cstr.data(), atos_cstr.size() - 1, fp);

            const std::string atos_str{atos_cstr.data()};
            size_t d_pos{atos_str.find(" (in ")};
            if (d_pos == std::string::npos) continue;
            const std::string function_part{atos_str.substr(0, d_pos)};

            d_pos = atos_str.find(") (", d_pos);
            if (d_pos == std::string::npos) continue;
            std::string source_part{atos_str.substr(d_pos + 3)};
            source_part = source_part.substr(0, source_part.size() - 2);

            t_snprintf(output_buf, avail_len, cur_len, msg_len, "%s at %s\n", function_part.c_str(),
                       source_part.c_str());
        }
    }

    return cur_len;
}
#endif // __APPLE__

size_t stack_interpret_other([[maybe_unused]] const void* const* const stack_ptr, const char* const* const stack_msg,
                             const size_t stack_size, char* const output_buf, const size_t output_buflen,
                             [[maybe_unused]] const bool trim_internal) {
    size_t cur_len{0};
    [[maybe_unused]] size_t frame_num{0};

    // NOTE: starting from 1, skipping this frame.
    for (size_t i{1}; i < stack_size; ++i) {
        // On non-Linux platform, just use the raw symbols.
        size_t msg_len{0};
        size_t avail_len{output_buflen};
        t_snprintf(output_buf, avail_len, cur_len, msg_len, "%s\n", stack_msg[i]);
    }
    return cur_len;
}
