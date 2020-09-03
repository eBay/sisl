/*
 * Logging.h
 *
 * Copyright (c) 2018 by eBay Corporation
 */

#pragma once

#define SPDLOG_FUNCTION __PRETTY_FUNCTION__
#define SPDLOG_NO_NAME
#include <cstdio>
#include <memory>
#include <numeric>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <csignal>
extern "C" {
#include <dlfcn.h>
}
#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/fmt/ostr.h>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/push_front.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/preprocessor/variadic/to_tuple.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <nlohmann/json.hpp>

// The following constexpr's are used to extract the filename
// from the full path during compile time.
constexpr const char* str_end(const char* str) { return *str ? str_end(str + 1) : str; }

constexpr bool str_slant(const char* str) { return *str == '/' ? true : (*str ? str_slant(str + 1) : false); }

constexpr const char* r_slant(const char* str) { return *str == '/' ? (str + 1) : r_slant(str - 1); }
constexpr const char* file_name(const char* str) { return str_slant(str) ? r_slant(str_end(str)) : str; }

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
#ifndef LOGGING_PREDICT_BRANCH_NOT_TAKEN
#if defined(__clang__) || defined(__GNUC__)
#define LOGGING_PREDICT_BRANCH_NOT_TAKEN(x) (__builtin_expect(x, 0))
#else
#define LOGGING_PREDICT_BRANCH_NOT_TAKEN(x) x
#endif
#endif

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

#define LOGTRACEMOD(mod, msg, ...)                                                                                     \
    if (auto& _l = sds_logging::GetLogger(); _l && LEVELCHECK(mod, spdlog::level::level_enum::trace)) {                \
        _l->trace(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                                \
    }

#define LOGDEBUGMOD(mod, msg, ...)                                                                                     \
    if (auto& _l = sds_logging::GetLogger(); _l && LEVELCHECK(mod, spdlog::level::level_enum::debug)) {                \
        _l->debug(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                                \
    }

#define LOGINFOMOD(mod, msg, ...)                                                                                      \
    if (auto& _l = sds_logging::GetLogger(); _l && LEVELCHECK(mod, spdlog::level::level_enum::info)) {                 \
        _l->info(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                                 \
    }

#define LOGWARNMOD(mod, msg, ...)                                                                                      \
    if (auto& _l = sds_logging::GetLogger(); _l && LEVELCHECK(mod, spdlog::level::level_enum::warn)) {                 \
        _l->warn(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                                 \
    }

#define LOGERRORMOD(mod, msg, ...)                                                                                     \
    if (auto& _l = sds_logging::GetLogger(); _l && LEVELCHECK(mod, spdlog::level::level_enum::err)) {                  \
        _l->error(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                                \
    }

#define LOGCRITICALMOD(mod, msg, ...)                                                                                  \
    if (auto& _cl = sds_logging::GetCriticalLogger(); _cl && LEVELCHECK(mod, spdlog::level::level_enum::critical)) {   \
        _cl->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                            \
    }                                                                                                                  \
    if (auto& _l = sds_logging::GetLogger(); _l && LEVELCHECK(mod, spdlog::level::level_enum::critical)) {             \
        _l->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                             \
    }

/* Extension macros to support custom formatting of messages */
#define _LOG_WITH_CUSTOM_FORMATTER(lvl, method, mod, logger, formatter, msg, ...)                                      \
    if (auto& _l = logger; _l && LEVELCHECK(mod, spdlog::level::level_enum::lvl)) {                                    \
        fmt::memory_buffer _log_buf;                                                                                   \
        const auto& cb = formatter;                                                                                    \
        cb(_log_buf, msg, ##__VA_ARGS__);                                                                              \
        fmt::format_to(_log_buf, "{}", (char)0);                                                                       \
        _l->method(_log_buf.data());                                                                                   \
    }

#define LOGTRACEMOD_FMT(mod, formatter, msg, ...)                                                                      \
    _LOG_WITH_CUSTOM_FORMATTER(trace, trace, mod, sds_logging::GetLogger(), formatter, msg, ##__VA_ARGS__)

#define LOGDEBUGMOD_FMT(mod, formatter, msg, ...)                                                                      \
    _LOG_WITH_CUSTOM_FORMATTER(debug, debug, mod, sds_logging::GetLogger(), formatter, msg, ##__VA_ARGS__)

#define LOGINFOMOD_FMT(mod, formatter, msg, ...)                                                                       \
    _LOG_WITH_CUSTOM_FORMATTER(info, info, mod, sds_logging::GetLogger(), formatter, msg, ##__VA_ARGS__)

#define LOGWARNMOD_FMT(mod, formatter, msg, ...)                                                                       \
    _LOG_WITH_CUSTOM_FORMATTER(warn, warn, mod, sds_logging::GetLogger(), formatter, msg, ##__VA_ARGS__)

#define LOGERRORMOD_FMT(mod, formatter, msg, ...)                                                                      \
    _LOG_WITH_CUSTOM_FORMATTER(err, error, mod, sds_logging::GetLogger(), formatter, msg, ##__VA_ARGS__)

#define LOGCRITICALMOD_FMT(mod, formatter, msg, ...)                                                                   \
    _LOG_WITH_CUSTOM_FORMATTER(critical, critical, mod, sds_logging::GetCriticalLogger(), formatter, msg,              \
                               ##__VA_ARGS__)                                                                          \
    _LOG_WITH_CUSTOM_FORMATTER(critical, critical, mod, sds_logging::GetLogger(), formatter, msg, ##__VA_ARGS__)

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
        auto& _cl = sds_logging::GetCriticalLogger();                                                                  \
        _cl->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                            \
        _cl->flush();                                                                                                  \
                                                                                                                       \
        auto& _l = sds_logging::GetLogger();                                                                           \
        _l->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__);                                             \
        _l->flush();                                                                                                   \
    }

#define _ABORT_OR_DUMP(is_log_assert)                                                                                  \
    assert(0);                                                                                                         \
    if (is_log_assert) {                                                                                               \
        if (sds_logging::is_crash_handler_installed()) {                                                               \
            sds_logging::log_stack_trace(false);                                                                       \
        }                                                                                                              \
    } else {                                                                                                           \
        abort();                                                                                                       \
    }

#define _LOG_AND_ASSERT(is_log_assert, msg, ...)                                                                       \
    LOGCRITICAL(msg, ##__VA_ARGS__);                                                                                   \
    _ABORT_OR_DUMP(is_log_assert)

#define _LOG_AND_ASSERT_FMT(is_log_assert, formatter, msg, ...)                                                        \
    _LOG_WITH_CUSTOM_FORMATTER(critical, critical, base, sds_logging::GetCriticalLogger(), formatter, msg,             \
                               ##__VA_ARGS__)                                                                          \
    _ABORT_OR_DUMP(is_log_assert)

#define LOGDFATAL(msg, ...) _LOG_AND_ASSERT(1, msg, ##__VA_ARGS__)
#define LOGFATAL(msg, ...) _LOG_AND_ASSERT(0, msg, ##__VA_ARGS__)

/*
 * RELEASE_ASSERT:  If condition is not met: Logs the message, aborts both in release and debug build
 * LOGMSG_ASSERT:   If condition is not met: Logs the message with stack trace, aborts in debug build only.
 * DEBUG_ASSERT:    No-op in release build, for debug build, if condition is not met, logs the message and aborts
 */
#define _GENERIC_ASSERT(is_log_assert, cond, formatter, msg, ...)                                                      \
    if (LOGGING_PREDICT_BRANCH_NOT_TAKEN(!(cond))) {                                                                   \
        _LOG_AND_ASSERT_FMT(is_log_assert, formatter, msg, ##__VA_ARGS__);                                             \
    }

#define _FMT_LOG_MSG(...) sds_logging::format_log_msg(__VA_ARGS__).c_str()

#define RELEASE_ASSERT(cond, m, ...)                                                                                   \
    _GENERIC_ASSERT(                                                                                                   \
        0, cond, [](fmt::memory_buffer& buf, const char* msg, auto... args) { fmt::format_to(buf, msg, args...); }, m, \
        ##__VA_ARGS__)
#define RELEASE_ASSERT_FMT(cond, formatter, msg, ...) _GENERIC_ASSERT(0, cond, formatter, msg, ##__VA_ARGS__)
#define RELEASE_ASSERT_CMP(val1, cmp, val2, formatter, ...)                                                            \
    _GENERIC_ASSERT(0, ((val1)cmp(val2)), formatter, _FMT_LOG_MSG(__VA_ARGS__), val1, #cmp, val2)
#define RELEASE_ASSERT_CMP_DEFAULT_FMT(val1, cmp, val2, ...)                                                           \
    RELEASE_ASSERT_CMP(                                                                                                \
        val1, cmp, val2,                                                                                               \
        [](fmt::memory_buffer& buf, const char* msg, auto&&... args) {                                                 \
            sds_logging::_cmp_assert_with_msg(buf, msg, args...);                                                      \
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
        1, cond, [](fmt::memory_buffer& buf, const char* msg, auto&&... args) { fmt::format_to(buf, msg, args...); },  \
        m, ##__VA_ARGS__)
#define LOGMSG_ASSERT_FMT(cond, formatter, msg, ...) _GENERIC_ASSERT(1, cond, formatter, msg, ##__VA_ARGS__)
#define LOGMSG_ASSERT_CMP(val1, cmp, val2, formatter, ...)                                                             \
    _GENERIC_ASSERT(1, ((val1)cmp(val2)), formatter, sds_logging::format_log_msg(__VA_ARGS__).c_str(), val1, #cmp, val2)

#define LOGMSG_ASSERT_CMP_DEFAULT_FMT(val1, cmp, val2, ...)                                                            \
    LOGMSG_ASSERT_CMP(                                                                                                 \
        val1, cmp, val2,                                                                                               \
        [](fmt::memory_buffer& buf, const char* msg, auto&&... args) {                                                 \
            sds_logging::_cmp_assert_with_msg(buf, msg, args...);                                                      \
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

namespace sds_logging {
template < typename T >
using shared = std::shared_ptr< T >;

static constexpr uint32_t max_stacktrace_size() { return (64U * 1024U); }

#if defined(__linux__)
#define SIGUSR3 SIGRTMIN + 1
#define SIGUSR4 SIGUSR3 + 1
#else
#define SIGUSR3 SIGUSR1
#define SIGUSR4 SIGUSR2
#endif

class LoggerThreadContext {
public:
    LoggerThreadContext();
    ~LoggerThreadContext();

    static LoggerThreadContext& instance() {
        static thread_local LoggerThreadContext inst;
        return inst;
    }

    static std::mutex _logger_thread_mutex;
    static std::unordered_set< LoggerThreadContext* > _logger_thread_set;

    static void add_logger_thread(LoggerThreadContext* ctx) {
        std::unique_lock l(_logger_thread_mutex);
        _logger_thread_set.insert(ctx);
    }

    static void remove_logger_thread(LoggerThreadContext* ctx) {
        std::unique_lock l(_logger_thread_mutex);
        _logger_thread_set.erase(ctx);
    }

    std::shared_ptr< spdlog::logger > m_logger;
    std::shared_ptr< spdlog::logger > m_critical_logger;
    pthread_t m_thread_id;
    char m_stack_buff[max_stacktrace_size()];
};

#define logger_thread_ctx LoggerThreadContext::instance()
#define mythread_logger logger_thread_ctx.m_logger
#define mycritical_logger logger_thread_ctx.m_critical_logger

extern std::shared_ptr< spdlog::logger > glob_spdlog_logger;
extern std::shared_ptr< spdlog::logger > glob_critical_logger;
extern shared< spdlog::logger >& GetLogger() __attribute__((weak));
extern shared< spdlog::logger >& GetCriticalLogger() __attribute__((weak));
extern std::vector< std::string > glob_enabled_mods;
} // namespace sds_logging

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

#define SDS_LOGGING_DECL(...)                                                                                          \
    BOOST_PP_SEQ_FOR_EACH(MODLEVELDEC, spdlog::level::level_enum::off, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

#define SDS_LOGGING_INIT(...)                                                                                          \
    BOOST_PP_SEQ_FOR_EACH(                                                                                             \
        MODLEVELDEF, spdlog::level::level_enum::info,                                                                  \
        BOOST_PP_TUPLE_TO_SEQ(BOOST_PP_TUPLE_PUSH_FRONT(BOOST_PP_VARIADIC_TO_TUPLE(__VA_ARGS__), base)))               \
    namespace sds_logging {                                                                                            \
    std::shared_ptr< spdlog::logger > glob_spdlog_logger;                                                              \
    std::shared_ptr< spdlog::logger > glob_critical_logger;                                                            \
    std::vector< std::string > glob_enabled_mods = {                                                                   \
        MOD_LEVEL_STRING(, , base) BOOST_PP_SEQ_FOR_EACH(MOD_LEVEL_STRING, , BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))};  \
    std::mutex LoggerThreadContext::_logger_thread_mutex;                                                              \
    std::unordered_set< LoggerThreadContext* > LoggerThreadContext::_logger_thread_set;                                \
    }

namespace sds_logging {
typedef void (*sig_handler_t)(int, siginfo_t*, void*);

void SetLogger(std::string const& name, std::string const& pkg = BOOST_PP_STRINGIZE(PACKAGE_NAME),
               std::string const& ver = BOOST_PP_STRINGIZE(PACKAGE_VERSION));

void SetModuleLogLevel(const std::string& module_name, spdlog::level::level_enum level);
spdlog::level::level_enum GetModuleLogLevel(const std::string& module_name);
nlohmann::json GetAllModuleLogLevel();
void SetAllModuleLogLevel(spdlog::level::level_enum level);

void log_stack_trace(bool all_threads = false);
void install_signal_handler();
void add_signal_handler(int sig_num, std::string_view sig_name, sig_handler_t hdlr);
void install_crash_handler();
bool is_crash_handler_installed();
void override_setup_signals(const std::map< int, std::string > override_signals);
void restore_signal_handler_to_default();
void send_thread_signal(pthread_t thr, int sig_num);

template < typename... Args >
std::string format_log_msg(const char* fmt, const Args&... args) {
    fmt::memory_buffer buf;
    fmt::format_to(buf, fmt, args...);
    return to_string(buf);
}
std::string format_log_msg();

template < typename T1, typename T2, typename T3, typename... Args >
void _cmp_assert_with_msg(fmt::memory_buffer& buf, const char* msg, const T1& val1, const T2& op, const T3& val2,
                          Args&&... args) {
    fmt::format_to(buf, "******************** Assertion failure: =====> Expected '{}' to be {} to '{}' ", val1, op,
                   val2);
    fmt::format_to(buf, msg, args...);
}

template < typename... Args >
void default_cmp_assert_formatter(fmt::memory_buffer& buf, const char* msg, const Args&... args) {
    _cmp_assert_with_msg(buf, msg, args...);
}

} // namespace sds_logging
#define SDS_LOG_LEVEL(mod, lvl) BOOST_PP_CAT(module_level_, mod) = (lvl);
