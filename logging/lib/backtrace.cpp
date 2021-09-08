#include <iostream>

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
#include <dlfcn.h>
#include <sys/select.h>
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
    if (::_NSGetExecutablePath(path.data(), &size) != 0)
        return std::string{};

    return std::string{path.data()};
}

[[maybe_unused]] std::string get_file_part(const std::string& full_path) {
    const size_t pos{full_path.rfind("/")};
    if (pos == std::string::npos)
        return full_path;

    return full_path.substr(pos + 1, full_path.size() - pos - 1);
}

[[maybe_unused]] intptr_t image_slide(void) {
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
    if (!pos)
        return actual_addr;

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
    if (length == 0)
        return length;

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

template <typename... Args>
[[maybe_unused]] void log_message(const char* const format, Args&&... args) {
    auto& logger{sds_logging::GetLogger()};
    auto& critical_logger{sds_logging::GetCriticalLogger()};

    if (logger) {
        logger->critical(format, std::forward< Args >(args)...);
    }
    if (critical_logger) {
        critical_logger->critical(format, std::forward< Args >(args)...);
    }
}

#ifdef __linux__
std::pair< bool, uintptr_t > offset_symbol_address(const char* const symbol_str, const uintptr_t symbol_address) {
    bool status{false};
    uintptr_t offset_address{symbol_address};
    Dl_info symbol_info;

    const std::unique_ptr< void, std::function< void(void* const) > > obj_file{::dlopen(nullptr, RTLD_LAZY),
                                                                               [](void* const ptr) {
                                                                                   if (ptr)
                                                                                       ::dlclose(ptr);
                                                                               }};
    if (!obj_file) {
        return {status, offset_address};
    }

    void* const addr{::dlsym(obj_file.get(), symbol_str)};
    if (!addr) {
        return {status, offset_address};
    }

    // extract the symbolic information pointed by address
    if (!::dladdr(addr, &symbol_info)) {
        return {status, offset_address};
    }
    offset_address +=
        (reinterpret_cast< uintptr_t >(symbol_info.dli_saddr) - reinterpret_cast< uintptr_t >(symbol_info.dli_fbase)) -
        1;
    status = true;

    return {status, offset_address};
}

std::pair< const char*, const char* > convert_symbol_line(const char* const process_name,
                                                          const size_t process_name_length, const uintptr_t address,
                                                          const char* const symbol_name) {
    static constexpr size_t line_number_length{24};
    static constexpr std::array< char, 10 > s_pipe_unknown{"??\0??:?\0"};
    const char* mangled_name{s_pipe_unknown.data()};
    size_t mangled_name_length{2};
    const char* file_line{s_pipe_unknown.data() + 3};
    size_t file_line_length{4};

    if (process_name_length == 0)
        return {mangled_name, file_line};

    // form the command
    static constexpr std::array< char, 17 > prefix{"addr2line -f -e "};
    static std::array< char, prefix.size() + backtrace_detail::process_name_length + backtrace_detail::address_length >
        s_command;
    size_t command_length{prefix.size() - 1};
    std::memcpy(s_command.data(), prefix.data(), command_length);
    std::memcpy(s_command.data() + command_length, process_name, process_name_length);
    command_length += process_name_length;
    std::snprintf(s_command.data() + command_length, s_command.size() - command_length, " -a 0x%" PRIxPTR, address);
    // log_message("symbol_line with command {}", s_command.data());

    const std::unique_ptr< FILE, std::function< void(FILE* const) > > fp{::popen(s_command.data(), "r"),
                                                                         [](FILE* const ptr) {
                                                                             if (ptr)
                                                                                 ::pclose(ptr);
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
        constexpr uint64_t loop_wait_ms{50};
        constexpr size_t read_tries{3};
        constexpr size_t newlines_expected{3};
        std::array< const char*, newlines_expected > newline_positions;
        size_t total_bytes_read{0};
        size_t total_newlines{0};
        static std::array<
            char, backtrace_detail::symbol_name_length + backtrace_detail::process_name_length + line_number_length >
            s_pipe_data;
        for (size_t read_try{0}; (read_try < read_tries) && (total_newlines < newlines_expected); ++read_try) {
            if (waitOnPipe(loop_wait_ms)) {
                const size_t bytes{std::fread(s_pipe_data.data() + total_bytes_read, 1,
                                              s_pipe_data.size() - total_bytes_read, fp.get())};
                //std::printf("%.*s", static_cast<int>(bytes), s_pipe_data.data() + total_bytes_read); 
                // count new newlines and null terminate at those positions
                for (size_t byte_num{0}; byte_num < bytes; ++byte_num) {
                    const size_t offset{byte_num + total_bytes_read};
                    if (s_pipe_data[offset] == '\n') {
                        if (total_newlines < newlines_expected)
                            newline_positions[total_newlines] = &s_pipe_data[offset];
                        s_pipe_data[offset] = 0x00;
                        ++total_newlines;
                    }
                }
                total_bytes_read += bytes;
            }
        }
        //std::printf("\n");
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
            }
            else if (total_newlines == 2) {
                log_message("Pipe did not return expected number of newlines {}", total_newlines);
                mangled_name = newline_positions[0] + 1;
                mangled_name_length = static_cast< size_t >(newline_positions[1] - mangled_name);
                mangled_name_length = trim_whitespace(const_cast< char* >(mangled_name), mangled_name_length);
            }  else {
                log_message("Pipe did not return expected number of newlines {}", total_newlines);
            }
        } else {
            // no pipe data just continue
            log_message("No pipe data");
        }
    } else {
        // no pipe just continue
        log_message("Could not open pipe to resolve symbol_line with command {}", s_command.data());
    }
    if (std::strstr(mangled_name, "??")) {
        log_message("Could not resolve symbol_line with command {}", s_command.data());
    }

    // demangle the name
    static std::array< char, backtrace_detail::symbol_name_length > demangled_name;
    [[maybe_unused]] int status{-3}; // one of the arguments is invalid
    const std::unique_ptr< const char, std::function< void(const char* const) > > cxa_demangled_name{
        std::strstr(mangled_name, "??") ? nullptr : abi::__cxa_demangle(mangled_name, 0, 0, &status),
        [](const char* const ptr) {
            if (ptr)
                std::free(static_cast< void* >(const_cast< char* >(ptr)));
        }};
    if (!cxa_demangled_name) {
        if (status != -2) { // check that not a mangled name
            log_message("Could not demangle name {} error {}", mangled_name, status);
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

    // resolve file name if unknown
    static std::array< char, backtrace_detail::process_name_length + line_number_length > s_absolute_file_path;
    if (std::strstr(file_line, "??")) {
        if (const char* const path{::realpath(process_name, s_absolute_file_path.data())}) {
            // use the resolved file name and unknown line number
            std::strcat(s_absolute_file_path.data(), ":?");
        } else {
            log_message("Could not resolve real path for process {}", process_name);
            // use the default unknown
            std::memcpy(s_absolute_file_path.data(), file_line, file_line_length);
            s_absolute_file_path[file_line_length] = 0x00;
        }
    } else {
        // file name resolved already
        std::memcpy(s_absolute_file_path.data(), file_line, file_line_length);
        s_absolute_file_path[file_line_length] = 0x00;
    }

    return {demangled_name.data(), s_absolute_file_path.data()};
}
}

size_t stack_interpret_linux_file(const void* const* const stack_ptr, FILE* const stack_file, const size_t stack_size,
                                  char* const output_buf, const size_t output_buflen, const bool trim_internal) {
    static std::array< size_t, backtrace_detail::max_backtrace > s_output_line_start;
    size_t cur_len{0};
    std::rewind(stack_file);
    char c{0x00};

    /*
    while (!feof(stack_file)) {
        c = fgetc(stack_file);
        std::cout << c;
    }
    std::rewind(stack_file);
    */

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
                if (c == '\n')
                    return;
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

        static std::array< char, backtrace_detail::process_name_length > s_process_name;
        const size_t process_name_length{
            trim_whitespace(s_process_name.data(), extractName(s_process_name, std::array< char, 2 >{'(', '\n'}))};

        if (process_name_length == 0) {
            if (c != '\n')
                readTillEOL();
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
                    const auto [offset_result, offset_addr]{offset_symbol_address(s_symbol.data(), symbol_address)};
                    if (offset_result) {
                        actual_addr = offset_addr;
                    } else {
                        log_message("Could not resolve offset_symbol_address for symbol {} with address {}",
                                    s_symbol.data(), symbol_address);
                    }
                }
            }
        }

        const auto [demangled_name, file_line]{
            convert_symbol_line(s_process_name.data(), process_name_length, actual_addr, s_symbol.data())};
        if (!demangled_name || !file_line) {
            if (c != '\n')
                readTillEOL();
            continue;
        }

        if (trim_internal) {
            if (std::strstr(demangled_name, "sds_logging::bt_dumper") ||
                std::strstr(demangled_name, "sds_logging::crash_handler")) {
                trim_line = line_num;
            }
        }
        s_output_line_start[line_num] = cur_len;
        t_snprintf(output_buf, avail_len, cur_len, msg_len, "#%-3zu 0x%016" PRIxPTR " in %s at %s\n", line_num,
                   actual_addr, demangled_name, file_line);
        ++line_num;

        if (c != '\n')
            readTillEOL();
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
#endif

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

        size_t msg_len{0};
        size_t avail_len = output_buflen;

        t_snprintf(output_buf, avail_len, cur_len, msg_len, "#%-3zu %s in ", frame_num++, address.c_str());

        if (filename != exec_file) {
            // Dynamic library.
            int status;
            const std::unique_ptr< const char, std::function< void(const char* const) > > cc{
                abi::__cxa_demangle(func_mangled.c_str(), 0, 0, &status), [](const char* const ptr) {
                    if (ptr)
                        std::free(static_cast< void* >(const_cast< char* >(ptr)));
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
                                                                                     if (ptr)
                                                                                         ::pclose(ptr);
                                                                                 }};
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

            t_snprintf(output_buf, avail_len, cur_len, msg_len, "%s at %s\n", function_part.c_str(),
                       source_part.c_str());
        }
    }

    return cur_len;
}
#endif

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