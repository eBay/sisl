/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Brian Szymd, Harihara Kadayam
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
#pragma once

#define SPDLOG_FUNCTION __PRETTY_FUNCTION__
#define SPDLOG_NO_NAME

#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/push_front.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/preprocessor/variadic/to_tuple.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h> // NOTE: There is an ordering dependecy on this header and fmt headers below
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/fmt/ostr.h>

// The following constexpr's are used to extract the filename
// from the full path during compile time.
constexpr const char* str_end(const char* const str) { return (*str) ? str_end(str + 1) : str; }

constexpr bool str_slant(const char* const str) { return (*str == '/') ? true : (*str ? str_slant(str + 1) : false); }

constexpr const char* r_slant(const char* const str) { return (*str == '/') ? (str + 1) : r_slant(str - 1); }
constexpr const char* file_name(const char* const str) { return str_slant(str) ? r_slant(str_end(str)) : str; }

#ifndef PACKAGE_NAME
#define PACKAGE_NAME unknown
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION unknown
#endif

//////////////////////////////////////////////////////////////////////////////
// GCC can be told that a certain branch is not likely to be taken (for
// instance, a CHECK failure), and use that information in static analysis.
// Giving it this information can help it optimize for the common case in
// the absence of better information (ie. -fprofile-arcs).
//

#ifndef LOGGING_PREDICT_FALSE
#if defined(__clang__) || defined(__GNUC__)
#define LOGGING_PREDICT_FALSE(x) (__builtin_expect(x, 0))
#else
#define LOGGING_PREDICT_FALSE(x) x
#endif
#endif

#ifndef LOGGING_PREDICT_TRUE
#if defined(__clang__) || defined(__GNUC__)
#define LOGGING_PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#else
#define LOGGING_PREDICT_TRUE(x) x
#endif
#endif

#define LEVELCHECK(mod, lvl) (module_level_##mod <= (lvl))

#define LINEOUTPUTFORMAT "[{}:{}:{}] "
#define LINEOUTPUTARGS file_name(__FILE__), __LINE__, __FUNCTION__

#define LOGTRACEMOD_USING_LOGGER(mod, logger, msg, ...)                                                                \
    if (auto& _l{logger}; _l && LEVELCHECK(mod, spdlog::level::level_enum::trace)) {                                   \
        _l->trace(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                                \
    }

#define LOGDEBUGMOD_USING_LOGGER(mod, logger, msg, ...)                                                                \
    if (auto& _l{logger}; _l && LEVELCHECK(mod, spdlog::level::level_enum::debug)) {                                   \
        _l->debug(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                                \
    }

#define LOGINFOMOD_USING_LOGGER(mod, logger, msg, ...)                                                                 \
    if (auto& _l{logger}; _l && LEVELCHECK(mod, spdlog::level::level_enum::info)) {                                    \
        _l->info(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                                 \
    }

#define LOGWARNMOD_USING_LOGGER(mod, logger, msg, ...)                                                                 \
    if (auto& _l{logger}; _l && LEVELCHECK(mod, spdlog::level::level_enum::warn)) {                                    \
        _l->warn(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                                 \
    }

#define LOGERRORMOD_USING_LOGGER(mod, logger, msg, ...)                                                                \
    if (auto& _l{logger}; _l && LEVELCHECK(mod, spdlog::level::level_enum::err)) {                                     \
        _l->error(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                                \
    }

#define LOGCRITICALMOD_USING_LOGGER(mod, logger, msg, ...)                                                             \
    if (auto& _cl{sisl::logging::GetCriticalLogger()}; _cl && LEVELCHECK(mod, spdlog::level::level_enum::critical)) {  \
        _cl->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                            \
    }                                                                                                                  \
    if (auto& _l{logger}; _l && LEVELCHECK(mod, spdlog::level::level_enum::critical)) {                                \
        _l->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                             \
    }

#define LOGTRACEMOD(mod, msg, ...) LOGTRACEMOD_USING_LOGGER(mod, sisl::logging::GetLogger(), msg, ##__VA_ARGS__)
#define LOGDEBUGMOD(mod, msg, ...) LOGDEBUGMOD_USING_LOGGER(mod, sisl::logging::GetLogger(), msg, ##__VA_ARGS__)
#define LOGINFOMOD(mod, msg, ...) LOGINFOMOD_USING_LOGGER(mod, sisl::logging::GetLogger(), msg, ##__VA_ARGS__)
#define LOGWARNMOD(mod, msg, ...) LOGWARNMOD_USING_LOGGER(mod, sisl::logging::GetLogger(), msg, ##__VA_ARGS__)
#define LOGERRORMOD(mod, msg, ...) LOGERRORMOD_USING_LOGGER(mod, sisl::logging::GetLogger(), msg, ##__VA_ARGS__)
#define LOGCRITICALMOD(mod, msg, ...) LOGCRITICALMOD_USING_LOGGER(mod, sisl::logging::GetLogger(), msg, ##__VA_ARGS__)

/* Extension macros to support custom formatting of messages */
#if __cplusplus > 201703L
#define _LOG_WITH_CUSTOM_FORMATTER(lvl, method, mod, logger, is_flush, formatter, msg, ...)                            \
    if (auto& _l{logger}; _l && LEVELCHECK(mod, spdlog::level::level_enum::lvl)) {                                     \
        fmt::memory_buffer _log_buf{};                                                                                 \
        const auto& cb{formatter};                                                                                     \
        [[likely]] if (cb(_log_buf, msg __VA_OPT__(, ) __VA_ARGS__)) {                                                 \
            fmt::vformat_to(fmt::appender{_log_buf}, fmt::string_view{"{}"}, fmt::make_format_args('\0'));             \
            _l->method(_log_buf.data());                                                                               \
            if (is_flush) { _l->flush(); }                                                                             \
        }                                                                                                              \
    }
#else
#define _LOG_WITH_CUSTOM_FORMATTER(lvl, method, mod, logger, is_flush, formatter, msg, ...)                            \
    if (auto& _l{logger}; _l && LEVELCHECK(mod, spdlog::level::level_enum::lvl)) {                                     \
        fmt::memory_buffer _log_buf{};                                                                                 \
        const auto& cb{formatter};                                                                                     \
        if (LOGGING_PREDICT_TRUE(cb(_log_buf, msg __VA_OPT__(, ) __VA_ARGS__))) {                                      \
            fmt::vformat_to(fmt::appender{_log_buf}, fmt::string_view{"{}"}, fmt::make_format_args('\0'));             \
            _l->method(_log_buf.data());                                                                               \
            if (is_flush) { _l->flush(); }                                                                             \
        }                                                                                                              \
    }

#endif

// With custom formatter and custom logger
#define LOGTRACEMOD_FMT_USING_LOGGER(mod, formatter, logger, msg, ...)                                                 \
    _LOG_WITH_CUSTOM_FORMATTER(trace, trace, mod, logger, false, formatter, msg, ##__VA_ARGS__)

#define LOGDEBUGMOD_FMT_USING_LOGGER(mod, formatter, logger, msg, ...)                                                 \
    _LOG_WITH_CUSTOM_FORMATTER(debug, debug, mod, logger, false, formatter, msg, ##__VA_ARGS__)

#define LOGINFOMOD_FMT_USING_LOGGER(mod, formatter, logger, msg, ...)                                                  \
    _LOG_WITH_CUSTOM_FORMATTER(info, info, mod, logger, false, formatter, msg, ##__VA_ARGS__)

#define LOGWARNMOD_FMT_USING_LOGGER(mod, formatter, logger, msg, ...)                                                  \
    _LOG_WITH_CUSTOM_FORMATTER(warn, warn, mod, logger, false, formatter, msg, ##__VA_ARGS__)

#define LOGERRORMOD_FMT_USING_LOGGER(mod, formatter, logger, msg, ...)                                                 \
    _LOG_WITH_CUSTOM_FORMATTER(err, error, mod, logger, false, formatter, msg, ##__VA_ARGS__)

#define LOGCRITICALMOD_FMT_USING_LOGGER(mod, formatter, logger, msg, ...)                                              \
    _LOG_WITH_CUSTOM_FORMATTER(critical, critical, mod, logger, true, formatter, msg, ##__VA_ARGS__)

// With custom formatter
#define LOGTRACEMOD_FMT(mod, formatter, msg, ...)                                                                      \
    LOGTRACEMOD_FMT_USING_LOGGER(mod, formatter, sisl::logging::GetLogger(), msg, ##__VA_ARGS__)

#define LOGDEBUGMOD_FMT(mod, formatter, msg, ...)                                                                      \
    LOGDEBUGMOD_FMT_USING_LOGGER(mod, formatter, sisl::logging::GetLogger(), msg, ##__VA_ARGS__)

#define LOGINFOMOD_FMT(mod, formatter, msg, ...)                                                                       \
    LOGINFOMOD_FMT_USING_LOGGER(mod, formatter, sisl::logging::GetLogger(), msg, ##__VA_ARGS__)

#define LOGWARNMOD_FMT(mod, formatter, msg, ...)                                                                       \
    LOGWARNMOD_FMT_USING_LOGGER(mod, formatter, sisl::logging::GetLogger(), msg, ##__VA_ARGS__)

#define LOGERRORMOD_FMT(mod, formatter, msg, ...)                                                                      \
    LOGERRORMOD_FMT_USING_LOGGER(mod, formatter, sisl::logging::GetLogger(), msg, ##__VA_ARGS__)

#define LOGCRITICALMOD_FMT(mod, formatter, msg, ...)                                                                   \
    LOGCRITICALMOD_FMT_USING_LOGGER(mod, formatter, sisl::logging::GetCriticalLogger(), msg, ##__VA_ARGS__)            \
    LOGCRITICALMOD_FMT_USING_LOGGER(mod, formatter, sisl::logging::GetLogger(), msg, ##__VA_ARGS__)

#define LOGTRACE(msg, ...) LOGTRACEMOD(base, msg, ##__VA_ARGS__)
#define LOGDEBUG(msg, ...) LOGDEBUGMOD(base, msg, ##__VA_ARGS__)
#define LOGINFO(msg, ...) LOGINFOMOD(base, msg, ##__VA_ARGS__)
#define LOGWARN(msg, ...) LOGWARNMOD(base, msg, ##__VA_ARGS__)
#define LOGERROR(msg, ...) LOGERRORMOD(base, msg, ##__VA_ARGS__)
#define LOGCRITICAL(msg, ...) LOGCRITICALMOD(base, msg, ##__VA_ARGS__)

#ifndef NDEBUG
#define DLOGCRITICAL(...) LOGCRITICAL(__VA_ARGS__)
#define DLOGERROR(...) LOGERROR(__VA_ARGS__)
#define DLOGWARN(...) LOGWARN(__VA_ARGS__)
#define DLOGINFO(...) LOGINFO(__VA_ARGS__)
#define DLOGDEBUG(...) LOGDEBUG(__VA_ARGS__)
#define DLOGTRACE(...) LOGTRACE(__VA_ARGS__)

#define DLOGCRITICALMOD(...) LOGCRITICALMOD(__VA_ARGS__)
#define DLOGERRORMOD(...) LOGERRORMOD(__VA_ARGS__)
#define DLOGWARNMOD(...) LOGWARNMOD(__VA_ARGS__)
#define DLOGINFOMOD(...) LOGINFOMOD(__VA_ARGS__)
#define DLOGDEBUGMOD(...) LOGDEBUGMOD(__VA_ARGS__)
#define DLOGTRACEMOD(...) LOGTRACEMOD(__VA_ARGS__)
#else
#define DLOGCRITICAL(...)
#define DLOGERROR(...)
#define DLOGWARN(...)
#define DLOGINFO(...)
#define DLOGDEBUG(...)
#define DLOGTRACE(...)

#define DLOGCRITICALMOD(...)
#define DLOGERRORMOD(...)
#define DLOGWARNMOD(...)
#define DLOGINFOMOD(...)
#define DLOGDEBUGMOD(...)
#define DLOGTRACEMOD(...)
#endif

#define LOGCRITICAL_AND_FLUSH(msg, ...)                                                                                \
    {                                                                                                                  \
        auto& _cl{sisl::logging::GetCriticalLogger()};                                                                 \
        _cl->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                            \
        _cl->flush();                                                                                                  \
                                                                                                                       \
        auto& _l{sisl::logging::GetLogger()};                                                                          \
        _l->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                             \
        _l->flush();                                                                                                   \
    }

#define _ABORT_OR_DUMP(is_log_assert)                                                                                  \
    assert(0);                                                                                                         \
    if (is_log_assert) {                                                                                               \
        if (sisl::logging::is_crash_handler_installed()) { sisl::logging::log_stack_trace(false); }                    \
    } else {                                                                                                           \
        abort();                                                                                                       \
    }

#define _LOG_AND_ASSERT(is_log_assert, msg, ...)                                                                       \
    LOGCRITICAL_AND_FLUSH(msg, ##__VA_ARGS__);                                                                         \
    _ABORT_OR_DUMP(is_log_assert)

#define _LOG_AND_ASSERT_FMT(is_log_assert, formatter, msg, ...)                                                        \
    LOGCRITICALMOD_FMT(base, formatter, msg, ##__VA_ARGS__)                                                            \
    _ABORT_OR_DUMP(is_log_assert)

#define LOGDFATAL(msg, ...) _LOG_AND_ASSERT(1, msg, ##__VA_ARGS__)
#define LOGFATAL(msg, ...) _LOG_AND_ASSERT(0, msg, ##__VA_ARGS__)

/*
 * RELEASE_ASSERT:  If condition is not met: Logs the message, aborts both in release and debug build
 * LOGMSG_ASSERT:   If condition is not met: Logs the message with stack trace, aborts in debug build only.
 * DEBUG_ASSERT:    No-op in release build, for debug build, if condition is not met, logs the message and aborts
 */
#if __cplusplus > 201703L
#define _GENERIC_ASSERT(is_log_assert, cond, formatter, msg, ...)                                                      \
    [[unlikely]] if (!(cond)) { _LOG_AND_ASSERT_FMT(is_log_assert, formatter, msg, ##__VA_ARGS__); }
#else
#define _GENERIC_ASSERT(is_log_assert, cond, formatter, msg, ...)                                                      \
    if (LOGGING_PREDICT_FALSE(!(cond))) { _LOG_AND_ASSERT_FMT(is_log_assert, formatter, msg, ##__VA_ARGS__); }
#endif

#define _FMT_LOG_MSG(...) sisl::logging::format_log_msg(__VA_ARGS__).c_str()

#define RELEASE_ASSERT(cond, m, ...)                                                                                   \
    _GENERIC_ASSERT(                                                                                                   \
        0, cond,                                                                                                       \
        [](fmt::memory_buffer& buf, const char* msg, auto&&... args) -> bool {                                         \
            fmt::vformat_to(fmt::appender{buf}, fmt::string_view{msg},                                                 \
                            fmt::make_format_args(std::forward< decltype(args) >(args)...));                           \
            return true;                                                                                               \
        },                                                                                                             \
        m, ##__VA_ARGS__)
#define RELEASE_ASSERT_FMT(cond, formatter, msg, ...) _GENERIC_ASSERT(0, cond, formatter, msg, ##__VA_ARGS__)
#define RELEASE_ASSERT_CMP(val1, cmp, val2, formatter, ...)                                                            \
    _GENERIC_ASSERT(0, ((val1)cmp(val2)), formatter, _FMT_LOG_MSG(__VA_ARGS__), val1, #cmp, val2)
#define RELEASE_ASSERT_CMP_DEFAULT_FMT(val1, cmp, val2, ...)                                                           \
    RELEASE_ASSERT_CMP(                                                                                                \
        val1, cmp, val2,                                                                                               \
        [](fmt::memory_buffer& buf, const char* msg, auto&&... args) -> bool {                                         \
            sisl::logging::_cmp_assert_with_msg(buf, msg, std::forward< decltype(args) >(args)...);                    \
            return true;                                                                                               \
        },                                                                                                             \
        ##__VA_ARGS__)

#define RELEASE_ASSERT_EQ(val1, val2, ...) RELEASE_ASSERT_CMP_DEFAULT_FMT(val1, ==, val2, ##__VA_ARGS__)
#define RELEASE_ASSERT_NE(val1, val2, ...) RELEASE_ASSERT_CMP_DEFAULT_FMT(val1, !=, val2, ##__VA_ARGS__)
#define RELEASE_ASSERT_LE(val1, val2, ...) RELEASE_ASSERT_CMP_DEFAULT_FMT(val1, <=, val2, ##__VA_ARGS__)
#define RELEASE_ASSERT_LT(val1, val2, ...) RELEASE_ASSERT_CMP_DEFAULT_FMT(val1, <, val2, ##__VA_ARGS__)
#define RELEASE_ASSERT_GE(val1, val2, ...) RELEASE_ASSERT_CMP_DEFAULT_FMT(val1, >=, val2, ##__VA_ARGS__)
#define RELEASE_ASSERT_GT(val1, val2, ...) RELEASE_ASSERT_CMP_DEFAULT_FMT(val1, >, val2, ##__VA_ARGS__)
#define RELEASE_ASSERT_NOTNULL(val1, ...) RELEASE_ASSERT_CMP_DEFAULT_FMT(val1, !=, nullptr, ##__VA_ARGS__)

#define LOGMSG_ASSERT(cond, m, ...)                                                                                    \
    _GENERIC_ASSERT(                                                                                                   \
        1, cond,                                                                                                       \
        [](fmt::memory_buffer& buf, const char* msg, auto&&... args) -> bool {                                         \
            fmt::vformat_to(fmt::appender{buf}, fmt::string_view{msg},                                                 \
                            fmt::make_format_args(std::forward< decltype(args) >(args)...));                           \
            return true;                                                                                               \
        },                                                                                                             \
        m, ##__VA_ARGS__)
#define LOGMSG_ASSERT_FMT(cond, formatter, msg, ...) _GENERIC_ASSERT(1, cond, formatter, msg, ##__VA_ARGS__)
#define LOGMSG_ASSERT_CMP(val1, cmp, val2, formatter, ...)                                                             \
    _GENERIC_ASSERT(1, ((val1)cmp(val2)), formatter, sisl::logging::format_log_msg(__VA_ARGS__).c_str(), val1, #cmp,   \
                    val2)

#define LOGMSG_ASSERT_CMP_DEFAULT_FMT(val1, cmp, val2, ...)                                                            \
    LOGMSG_ASSERT_CMP(                                                                                                 \
        val1, cmp, val2,                                                                                               \
        [](fmt::memory_buffer& buf, const char* msg, auto&&... args) -> bool {                                         \
            sisl::logging::_cmp_assert_with_msg(buf, msg, std::forward< decltype(args) >(args)...);                    \
            return true;                                                                                               \
        },                                                                                                             \
        ##__VA_ARGS__)

#define LOGMSG_ASSERT_EQ(val1, val2, ...) LOGMSG_ASSERT_CMP_DEFAULT_FMT(val1, ==, val2, ##__VA_ARGS__)
#define LOGMSG_ASSERT_NE(val1, val2, ...) LOGMSG_ASSERT_CMP_DEFAULT_FMT(val1, !=, val2, ##__VA_ARGS__)
#define LOGMSG_ASSERT_LE(val1, val2, ...) LOGMSG_ASSERT_CMP_DEFAULT_FMT(val1, <=, val2, ##__VA_ARGS__)
#define LOGMSG_ASSERT_LT(val1, val2, ...) LOGMSG_ASSERT_CMP_DEFAULT_FMT(val1, <, val2, ##__VA_ARGS__)
#define LOGMSG_ASSERT_GE(val1, val2, ...) LOGMSG_ASSERT_CMP_DEFAULT_FMT(val1, >= val2, ##__VA_ARGS__)
#define LOGMSG_ASSERT_GT(val1, val2, ...) LOGMSG_ASSERT_CMP_DEFAULT_FMT(val1, >, val2, ##__VA_ARGS__)
#define LOGMSG_ASSERT_NOTNULL(val1, ...) LOGMSG_ASSERT_CMP_DEFAULT_FMT(val1, !=, nullptr, ##__VA_ARGS__)

#ifndef NDEBUG
#define DEBUG_ASSERT(cond, msg, ...) RELEASE_ASSERT(cond, msg, ##__VA_ARGS__)
#define DEBUG_ASSERT_CMP(...) RELEASE_ASSERT_CMP(__VA_ARGS__)
#define DEBUG_ASSERT_FMT(...) RELEASE_ASSERT_FMT(__VA_ARGS__)
#define DEBUG_ASSERT_EQ(val1, val2, ...) RELEASE_ASSERT_EQ(val1, val2, ##__VA_ARGS__)
#define DEBUG_ASSERT_NE(val1, val2, ...) RELEASE_ASSERT_NE(val1, val2, ##__VA_ARGS__)
#define DEBUG_ASSERT_LE(val1, val2, ...) RELEASE_ASSERT_LE(val1, val2, ##__VA_ARGS__)
#define DEBUG_ASSERT_LT(val1, val2, ...) RELEASE_ASSERT_LT(val1, val2, ##__VA_ARGS__)
#define DEBUG_ASSERT_GE(val1, val2, ...) RELEASE_ASSERT_GE(val1, val2, ##__VA_ARGS__)
#define DEBUG_ASSERT_GT(val1, val2, ...) RELEASE_ASSERT_GT(val1, val2, ##__VA_ARGS__)
#define DEBUG_ASSERT_NOTNULL(val1, ...) RELEASE_ASSERT_NOTNULL(val1, ##__VA_ARGS__)
#else
#define DEBUG_ASSERT(cond, msg, ...)
#define DEBUG_ASSERT_CMP(...)
#define DEBUG_ASSERT_FMT(...)
#define DEBUG_ASSERT_EQ(val1, val2, ...)
#define DEBUG_ASSERT_NE(val1, val2, ...)
#define DEBUG_ASSERT_LE(val1, val2, ...)
#define DEBUG_ASSERT_LT(val1, val2, ...)
#define DEBUG_ASSERT_GE(val1, val2, ...)
#define DEBUG_ASSERT_GT(val1, val2, ...)
#define DEBUG_ASSERT_NOTNULL(val1, ...)
#endif

namespace sisl {
namespace logging {

typedef spdlog::logger logger_t;

static constexpr uint32_t max_stacktrace_size() { return static_cast< uint32_t >(64) * 1024; }

#if defined(__linux__)
#define SIGUSR3 SIGRTMIN + 1
#define SIGUSR4 SIGUSR3 + 1
#else
#define SIGUSR3 SIGUSR1
#define SIGUSR4 SIGUSR2
#endif

class LoggerThreadContext {
public:
    LoggerThreadContext(const LoggerThreadContext&) = delete;
    LoggerThreadContext& operator=(const LoggerThreadContext&) = delete;
    LoggerThreadContext(LoggerThreadContext&&) noexcept = delete;
    LoggerThreadContext& operator=(LoggerThreadContext&&) noexcept = delete;
    ~LoggerThreadContext();

    static LoggerThreadContext& instance();

    static void add_logger_thread(LoggerThreadContext* const ctx);

    static void remove_logger_thread(LoggerThreadContext* const ctx);

    static std::mutex s_logger_thread_mutex;
    static std::unordered_set< LoggerThreadContext* > s_logger_thread_set;

    std::shared_ptr< spdlog::logger > m_logger;
    std::shared_ptr< spdlog::logger > m_critical_logger;
    pthread_t m_thread_id;

private:
    LoggerThreadContext();
};

class InitModules {
public:
    InitModules(std::initializer_list< const char* > list) { init_modules(list); }
    InitModules(const InitModules&) = delete;
    InitModules& operator=(const InitModules&) = delete;
    InitModules(InitModules&&) noexcept = delete;
    InitModules& operator=(InitModules&&) noexcept = delete;
    ~InitModules() = default;

private:
    void init_modules(std::initializer_list< const char* > mods_list);
};

#define logger_thread_ctx LoggerThreadContext::instance()
#define mythread_logger logger_thread_ctx.m_logger
#define mycritical_logger logger_thread_ctx.m_critical_logger

[[maybe_unused]] extern std::shared_ptr< spdlog::logger >& GetLogger();
[[maybe_unused]] extern std::shared_ptr< spdlog::logger >& GetCriticalLogger();

} // namespace logging
} // namespace sisl

#define MODLEVELDEC(r, _, module)                                                                                      \
    extern "C" {                                                                                                       \
    extern spdlog::level::level_enum BOOST_PP_CAT(module_level_, module);                                              \
    }
MODLEVELDEC(_, _, base)

#define MODLEVELDEF(r, l, module)                                                                                      \
    extern "C" {                                                                                                       \
    spdlog::level::level_enum BOOST_PP_CAT(module_level_, module){l};                                                  \
    }

#define MOD_LEVEL_STRING(r, _, module) BOOST_PP_STRINGIZE(module),

#define SISL_LOGGING_DECL(...)                                                                                         \
    BOOST_PP_SEQ_FOR_EACH(MODLEVELDEC, spdlog::level::level_enum::off, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

#define SISL_LOGGING_INIT(...)                                                                                         \
    BOOST_PP_SEQ_FOR_EACH(MODLEVELDEF, spdlog::level::level_enum::info,                                                \
                          BOOST_PP_TUPLE_TO_SEQ(BOOST_PP_VARIADIC_TO_TUPLE(__VA_ARGS__)))                              \
    sisl::logging::InitModules s_init_enabled_mods{                                                                    \
        BOOST_PP_SEQ_FOR_EACH(MOD_LEVEL_STRING, , BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))};

namespace sisl {
namespace logging {
typedef int SignalType;
typedef void (*sig_handler_t)(SignalType);

extern void
SetLogger(std::string const& name,
          std::string const& pkg = BOOST_PP_STRINGIZE(PACKAGE_NAME),
                                                      const std::string& ver = BOOST_PP_STRINGIZE(PACKAGE_VERSION));
extern std::shared_ptr< logger_t > CreateCustomLogger(const std::string& name, const std::string& extn,
                                                      const bool tee_to_stdout, const bool tee_to_stderr = false);
extern void SetLogPattern(const std::string& pattern,
                          const std::shared_ptr< sisl::logging::logger_t >& logger = nullptr);

extern void SetModuleLogLevel(const std::string& module_name, const spdlog::level::level_enum level);
extern spdlog::level::level_enum GetModuleLogLevel(const std::string& module_name);
extern nlohmann::json GetAllModuleLogLevel();
extern void SetAllModuleLogLevel(const spdlog::level::level_enum level);

extern void log_stack_trace(const bool all_threads = false);
extern bool install_signal_handler(const bool all_threads);
extern bool add_signal_handler(const SignalType sig_num, const std::string_view& sig_name, const sig_handler_t hdlr);
extern bool install_crash_handler(const bool all_threads = true);
extern bool is_crash_handler_installed();
// extern void override_setup_signals(const std::map< int, std::string >& override_signals);
extern bool restore_signal_handler(const SignalType sig_num);
extern bool restore_signal_handlers();
extern bool send_thread_signal(const pthread_t thr, const SignalType sig_num);

template < typename... Args >
std::string format_log_msg(const char* const msg, Args&&... args) {
    fmt::memory_buffer buf{};
    fmt::vformat_to(fmt::appender{buf}, fmt::string_view{msg}, fmt::make_format_args(std::forward< Args >(args)...));
    return fmt::to_string(buf);
}
extern std::string format_log_msg();

template < typename T1, typename T2, typename T3, typename... Args >
void _cmp_assert_with_msg(fmt::memory_buffer& buf, const char* const msg, T1&& val1, T2&& op, T3&& val2,
                          Args&&... args) {

    fmt::vformat_to(fmt::appender{buf},
                    fmt::string_view{"******************** Assertion failure: =====> Expected '{}' to be {} to '{}' "},
                    fmt::make_format_args(std::forward< T1 >(val1), std::forward< T2 >(op), std::forward< T3 >(val2)));
    fmt::vformat_to(fmt::appender{buf}, fmt::string_view{msg}, fmt::make_format_args(std::forward< Args >(args)...));
}

template < typename... Args >
void default_cmp_assert_formatter(fmt::memory_buffer& buf, const char* const msg, Args&&... args) {
    _cmp_assert_with_msg(buf, msg, std::forward< Args >(args)...);
}

} // namespace logging
} // namespace sisl
#define SISL_LOG_LEVEL(mod, lvl) BOOST_PP_CAT(module_level_, mod) = (lvl);
