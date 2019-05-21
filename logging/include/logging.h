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
extern "C" {
#include <dlfcn.h>
}
#include <spdlog/spdlog.h>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/push_front.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/preprocessor/variadic/to_tuple.hpp>

// The following constexpr's are used to extract the filename
// from the full path during compile time.
constexpr const char* str_end(const char *str) {
    return *str ? str_end(str + 1) : str;
}

constexpr bool str_slant(const char *str) {
    return *str == '/' ? true : (*str ? str_slant(str + 1) : false);
}

constexpr const char* r_slant(const char* str) {
    return *str == '/' ? (str + 1) : r_slant(str - 1);
}
constexpr const char* file_name(const char* str) {
    return str_slant(str) ? r_slant(str_end(str)) : str;
}

#ifndef PACKAGE_NAME
#define PACKAGE_NAME    unknown
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION unknown
#endif

#define LEVELCHECK(l, mod, lvl) (l && module_level_##mod <= (lvl))

#define LINEOUTPUTFORMAT "[{}:{}:{}] "
#define LINEOUTPUTARGS file_name(__FILE__), __LINE__, __FUNCTION__
#define LOGGER (sds_logging::sds_thread_logger ? sds_logging::sds_thread_logger : sds_logging::sds_thread_logger = sds_logging::GetLogger())

#define LOGTRACEMOD(mod, msg, ...)      if (auto _l = LOGGER; \
                                                _l && LEVELCHECK(_l, mod, spdlog::level::level_enum::trace)) \
                                            _l->trace(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGDEBUGMOD(mod, msg, ...)      if (auto _l = LOGGER; \
                                                _l && LEVELCHECK(_l, mod, spdlog::level::level_enum::debug)) \
                                            _l->debug(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGINFOMOD(mod, msg, ...)       if (auto _l = LOGGER; \
                                                _l && LEVELCHECK(_l, mod, spdlog::level::level_enum::info)) \
                                            _l->info(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGWARNMOD(mod, msg, ...)       if (auto _l = LOGGER; \
                                                _l && LEVELCHECK(_l, mod, spdlog::level::level_enum::warn)) \
                                            _l->warn(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGERRORMOD(mod, msg, ...)      if (auto _l = LOGGER; \
                                                _l && LEVELCHECK(_l, mod, spdlog::level::level_enum::err)) \
                                            _l->error(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGCRITICALMOD(mod, msg, ...)   if (auto _l = LOGGER; \
                                                _l && LEVELCHECK(_l, mod, spdlog::level::level_enum::critical)) \
                                            _l->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)

#define LOGTRACE(msg, ...)      LOGTRACEMOD(base, msg, ##__VA_ARGS__)
#define LOGDEBUG(msg, ...)      LOGDEBUGMOD(base, msg, ##__VA_ARGS__)
#define LOGINFO(msg, ...)       LOGINFOMOD(base, msg, ##__VA_ARGS__)
#define LOGWARN(msg, ...)       LOGWARNMOD(base, msg, ##__VA_ARGS__)
#define LOGERROR(msg, ...)      LOGERRORMOD(base, msg, ##__VA_ARGS__)
#define LOGCRITICAL(msg, ...)   LOGCRITICALMOD(base, msg, ##__VA_ARGS__)

#define LOGCRITICAL_AND_FLUSH(msg, ...) {auto _l = LOGGER; _l->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__); _l->flush(); }

#ifndef NDEBUG
#define LOGFATAL(msg, ...) \
        LOGCRITICAL_AND_FLUSH(msg, __VA_ARGS__); \
	assert(0);
#else
#define LOGFATAL(msg, ...) \
        LOGCRITICAL_AND_FLUSH(msg, __VA_ARGS__); \
        abort();
#endif

#define LOGDFATAL_IF(cond, msg, ...) \
	if (cond) { \
	   LOGFATAL(msg, __VA_ARGS__); \
	}

	//sds_logging::internal::signalHandler(6, nullptr, nullptr); 
	//spdlog::shutdown(); 
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

#define CONFIRM_OP(name, op, val1, val2) \
    if (!(LOGGING_PREDICT_BRANCH_NOT_TAKEN((val1) op (val2)))) { \
        LOGFATAL("Assertion failure: Expected '{}' to be {} to '{}'", val1, #op, val2); \
    }
#define CONFIRM_EQ(val1, val2) CONFIRM_OP(_EQ, ==, val1, val2)
#define CONFIRM_NE(val1, val2) CONFIRM_OP(_NE, !=, val1, val2)
#define CONFIRM_LE(val1, val2) CONFIRM_OP(_LE, <=, val1, val2)
#define CONFIRM_LT(val1, val2) CONFIRM_OP(_LT, < , val1, val2)
#define CONFIRM_GE(val1, val2) CONFIRM_OP(_GE, >=, val1, val2)
#define CONFIRM_GT(val1, val2) CONFIRM_OP(_GT, > , val1, val2)

#ifndef NDEBUG
#define DCONFIRM_EQ(val1, val2) CONFIRM_OP(_EQ, ==, val1, val2)
#define DCONFIRM_NE(val1, val2) CONFIRM_OP(_NE, !=, val1, val2)
#define DCONFIRM_LE(val1, val2) CONFIRM_OP(_LE, <=, val1, val2)
#define DCONFIRM_LT(val1, val2) CONFIRM_OP(_LT, < , val1, val2)
#define DCONFIRM_GE(val1, val2) CONFIRM_OP(_GE, >=, val1, val2)
#define DCONFIRM_GT(val1, val2) CONFIRM_OP(_GT, > , val1, val2)
#else
#define DCONFIRM_EQ(val1, val2)
#define DCONFIRM_NE(val1, val2)
#define DCONFIRM_LE(val1, val2) 
#define DCONFIRM_LT(val1, val2)
#define DCONFIRM_GE(val1, val2) 
#define DCONFIRM_GT(val1, val2) 
#endif

namespace sds_logging {
template <typename T>
using shared = std::shared_ptr<T>;


extern sds_logging::shared<spdlog::logger> logger_;
extern thread_local shared<spdlog::logger> sds_thread_logger;
extern shared<spdlog::logger> GetLogger() __attribute__((weak));
}

#define MODLEVELDEC(r, _, module) \
    extern "C" {                  \
        extern spdlog::level::level_enum BOOST_PP_CAT(module_level_, module); \
    }
MODLEVELDEC(_, _, base)

#define MODLEVELDEF(r, l, module) \
    extern "C" {                  \
        spdlog::level::level_enum BOOST_PP_CAT(module_level_, module) {l}; \
    }

#define SDS_LOGGING_DECL(...)                                                   \
   BOOST_PP_SEQ_FOR_EACH(MODLEVELDEC, spdlog::level::level_enum::off, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

#define SDS_LOGGING_INIT(...)                                                           \
   BOOST_PP_SEQ_FOR_EACH(MODLEVELDEF, spdlog::level::level_enum::warn, BOOST_PP_TUPLE_TO_SEQ(BOOST_PP_TUPLE_PUSH_FRONT(BOOST_PP_VARIADIC_TO_TUPLE(__VA_ARGS__), base))) \
   namespace sds_logging { shared<spdlog::logger> logger_; }

namespace sds_logging {

void SetLogger(std::string const& name,
               std::string const& pkg = BOOST_PP_STRINGIZE(PACKAGE_NAME),
               std::string const& ver = BOOST_PP_STRINGIZE(PACKAGE_VERSION));
}

#define SDS_LOG_LEVEL(mod, lvl) BOOST_PP_CAT(module_level_, mod) = (lvl);
