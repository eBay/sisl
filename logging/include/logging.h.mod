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
#define LOGFATAL(msg, ...) \
        LOGCRITICAL_AND_FLUSH(msg, __VA_ARGS__); \
        assert(0);

#define LOGDFATAL_IF(cond, msg, ...) \
	if (cond) { \
        LOGFATAL(msg, __VA_ARGS__); \
	}

//////////////////////////////////////////////////////////////////////////////
// GCC can be told that a certain branch is not likely to be taken (for
// instance, a CHECK failure), and use that information in static analysis.
// Giving it this information can help it optimize for the common case in
// the absence of better information (ie. -fprofile-arcs).
//
#ifndef LOGGING_PREDICT_BRANCH_NOT_TAKEN
#if @ac_cv_have___builtin_expect@
#define LOGGING_PREDICT_BRANCH_NOT_TAKEN(x) (__builtin_expect(x, 0))
#else
#define LOGGING_PREDICT_BRANCH_NOT_TAKEN(x) x
#endif
#endif

#ifndef LOGGING_PREDICT_FALSE
#if @ac_cv_have___builtin_expect@
#define LOGGING_PREDICT_FALSE(x) (__builtin_expect(x, 0))
#else
#define LOGGING_PREDICT_FALSE(x) x
#endif
#endif

#ifndef LOGGING_PREDICT_TRUE
#if @ac_cv_have___builtin_expect@
#define LOGGING_PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#else
#define LOGGING_PREDICT_TRUE(x) x
#endif
#endif

#define ASSERT_OP(name, op, vql1, val2) \
    if (!(LOGGING_PREDICT_BRANCH_NOT_TAKEN((val1) op (val2)))) { \
        LOGFATAL("Assertion failure: Expected {} to be {} to {}", val1, #op, val2); \
    }
#define ASSERT_EQ(val1, val2) ASSERT_OP(_EQ, ==, val1, val2)
#define ASSERT_NE(val1, val2) ASSERT_OP(_NE, !=, val1, val2)
#define ASSERT_LE(val1, val2) ASSERT_OP(_LE, <=, val1, val2)
#define ASSERT_LT(val1, val2) ASSERT_OP(_LT, < , val1, val2)
#define ASSERT_GE(val1, val2) ASSERT_OP(_GE, >=, val1, val2)
#define ASSERT_GT(val1, val2) ASSERT_OP(_GT, > , val1, val2)


/******************************/
namespace sds_logging {
// A container for a string pointer which can be evaluated to a bool -
// true iff the pointer is NULL.
struct CheckOpString {
  CheckOpString(std::string* str) : str_(str) { }
  // No destructor: if str_ is non-NULL, we're about to LOG(FATAL),
  // so there's no point in cleaning up str_.
  operator bool() const {
    return LOGGING_PREDICT_BRANCH_NOT_TAKEN(str_ != NULL);
  }
  std::string* str_;
};

// A helper class for formatting "expr (V1 vs. V2)" in a CHECK_XX
// statement.  See MakeCheckOpString for sample usage.  Other
// approaches were considered: use of a template method (e.g.,
// base::BuildCheckOpString(exprtext, base::Print<T1>, &v1,
// base::Print<T2>, &v2), however this approach has complications
// related to volatile arguments and function-pointer arguments).
class CheckOpMessageBuilder {
 public:
  // Inserts "exprtext" and " (" to the stream.
  explicit CheckOpMessageBuilder(const char *exprtext);
  // Deletes "stream_".
  ~CheckOpMessageBuilder();
  // For inserting the first variable.
  std::ostream* ForVar1() { return stream_; }
  // For inserting the second variable (adds an intermediate " vs. ").
  std::ostream* ForVar2();
  // Get the result (inserts the closing ")").
  std::string* NewString();

 private:
  std::ostringstream *stream_;
};

template <typename T1, typename T2>
std::string* MakeCheckOpString(const T1& v1, const T2& v2, const char* exprtext) {
  CheckOpMessageBuilder comb(exprtext);
  MakeCheckOpValueString(comb.ForVar1(), v1);
  MakeCheckOpValueString(comb.ForVar2(), v2);
  return comb.NewString();
}
}

// Helper functions for CHECK_OP macro.
// The (int, int) specialization works around the issue that the compiler
// will not instantiate the template version of the function on values of
// unnamed enum type - see comment below.
#define DEFINE_CHECK_OP_IMPL(name, op) \
  template <typename T1, typename T2> \
  inline std::string* name##Impl(const T1& v1, const T2& v2, const char* exprtext) { \
    if (LOGGING_PREDICT_TRUE(v1 op v2)) return NULL; \
    else return sds_logging::MakeCheckOpString(v1, v2, exprtext); \
  } \
  inline std::string* name##Impl(int v1, int v2, const char* exprtext) { \
    return name##Impl<int, int>(v1, v2, exprtext); \
  }

// We use the full name Check_EQ, Check_NE, etc. in case the file including
// base/logging.h provides its own #defines for the simpler names EQ, NE, etc.
// This happens if, for example, those are used as token names in a
// yacc grammar.
DEFINE_CHECK_OP_IMPL(Check_EQ, ==)  // Compilation error with CHECK_EQ(NULL, x)?
DEFINE_CHECK_OP_IMPL(Check_NE, !=)  // Use CHECK(x == NULL) instead.
DEFINE_CHECK_OP_IMPL(Check_LE, <=)
DEFINE_CHECK_OP_IMPL(Check_LT, < )
DEFINE_CHECK_OP_IMPL(Check_GE, >=)
DEFINE_CHECK_OP_IMPL(Check_GT, > )
#undef DEFINE_CHECK_OP_IMPL

// Helper macro for binary operators.
// Don't use this macro directly in your code, use CHECK_EQ et al below.

#if defined(STATIC_ANALYSIS)
// Only for static analysis tool to know that it is equivalent to assert
#define CHECK_OP_LOG(name, op, val1, val2, log) CHECK((val1) op (val2))
#elif DCHECK_IS_ON()
// In debug mode, avoid constructing CheckOpStrings if possible,
// to reduce the overhead of CHECK statments by 2x.
// Real DCHECK-heavy tests have seen 1.5x speedups.

// The meaning of "string" might be different between now and
// when this macro gets invoked (e.g., if someone is experimenting
// with other string implementations that get defined after this
// file is included).  Save the current meaning now and use it
// in the macro.
typedef std::string _Check_string;
#define CHECK_OP_LOG(name, op, val1, val2, log)                         \
  while (@ac_google_namespace@::_Check_string* _result =                \
         @ac_google_namespace@::Check##name##Impl(                      \
             @ac_google_namespace@::GetReferenceableValue(val1),        \
             @ac_google_namespace@::GetReferenceableValue(val2),        \
             #val1 " " #op " " #val2))                                  \
    log(__FILE__, __LINE__,                                             \
        @ac_google_namespace@::CheckOpString(_result)).stream()
#else
// In optimized mode, use CheckOpString to hint to compiler that
// the while condition is unlikely.
#define CHECK_OP_LOG(name, op, val1, val2, log)                         \
  while (sds_logging::CheckOpString _result =                           \
         sds_logging::Check##name##Impl(                                \
             sds_logging::GetReferenceableValue(val1),                  \
             sds_logging::GetReferenceableValue(val2),                  \
             #val1 " " #op " " #val2))                                  \
    log(__FILE__, __LINE__, _result).stream()
#endif  // STATIC_ANALYSIS, DCHECK_IS_ON()

#define CHECK_OP(name, op, val1, val2) \
  CHECK_OP_LOG(name, op, val1, val2, )

// Equality/Inequality checks - compare two values, and log a FATAL message
// including the two values when the result is not as expected.  The values
// must have operator<<(ostream, ...) defined.
//
// You may append to the error message like so:
//   CHECK_NE(1, 2) << ": The world must be ending!";
//
// We are very careful to ensure that each argument is evaluated exactly
// once, and that anything which is legal to pass as a function argument is
// legal here.  In particular, the arguments may be temporary expressions
// which will end up being destroyed at the end of the apparent statement,
// for example:
//   CHECK_EQ(string("abc")[1], 'b');
//
// WARNING: These don't compile correctly if one of the arguments is a pointer
// and the other is NULL. To work around this, simply static_cast NULL to the
// type of the desired pointer.

#define CHECK_EQ(val1, val2) CHECK_OP(_EQ, ==, val1, val2)
#define CHECK_NE(val1, val2) CHECK_OP(_NE, !=, val1, val2)
#define CHECK_LE(val1, val2) CHECK_OP(_LE, <=, val1, val2)
#define CHECK_LT(val1, val2) CHECK_OP(_LT, < , val1, val2)
#define CHECK_GE(val1, val2) CHECK_OP(_GE, >=, val1, val2)
#define CHECK_GT(val1, val2) CHECK_OP(_GT, > , val1, val2)

// Check that the input is non NULL.  This very useful in constructor
// initializer lists.

#define CHECK_NOTNULL(val) \
  @ac_google_namespace@::CheckNotNull(__FILE__, __LINE__, "'" #val "' Must be non NULL", (val))

// Helper functions for string comparisons.
// To avoid bloat, the definitions are in logging.cc.
#define DECLARE_CHECK_STROP_IMPL(func, expected) \
  GOOGLE_GLOG_DLL_DECL std::string* Check##func##expected##Impl( \
      const char* s1, const char* s2, const char* names);
DECLARE_CHECK_STROP_IMPL(strcmp, true)
DECLARE_CHECK_STROP_IMPL(strcmp, false)
DECLARE_CHECK_STROP_IMPL(strcasecmp, true)
DECLARE_CHECK_STROP_IMPL(strcasecmp, false)
#undef DECLARE_CHECK_STROP_IMPL

// Helper macro for string comparisons.
// Don't use this macro directly in your code, use CHECK_STREQ et al below.
#define CHECK_STROP(func, op, expected, s1, s2) \
  while (@ac_google_namespace@::CheckOpString _result = \
         @ac_google_namespace@::Check##func##expected##Impl((s1), (s2), \
                                     #s1 " " #op " " #s2)) \
    LOG(FATAL) << *_result.str_


// String (char*) equality/inequality checks.
// CASE versions are case-insensitive.
//
// Note that "s1" and "s2" may be temporary strings which are destroyed
// by the compiler at the end of the current "full expression"
// (e.g. CHECK_STREQ(Foo().c_str(), Bar().c_str())).

#define CHECK_STREQ(s1, s2) CHECK_STROP(strcmp, ==, true, s1, s2)
#define CHECK_STRNE(s1, s2) CHECK_STROP(strcmp, !=, false, s1, s2)
#define CHECK_STRCASEEQ(s1, s2) CHECK_STROP(strcasecmp, ==, true, s1, s2)
#define CHECK_STRCASENE(s1, s2) CHECK_STROP(strcasecmp, !=, false, s1, s2)

#define CHECK_INDEX(I,A) CHECK(I < (sizeof(A)/sizeof(A[0])))
#define CHECK_BOUND(B,A) CHECK(B <= (sizeof(A)/sizeof(A[0])))

#define CHECK_DOUBLE_EQ(val1, val2)              \
  do {                                           \
    CHECK_LE((val1), (val2)+0.000000000000001L); \
    CHECK_GE((val1), (val2)-0.000000000000001L); \
  } while (0)

#define CHECK_NEAR(val1, val2, margin)           \
  do {                                           \
    CHECK_LE((val1), (val2)+(margin));           \
    CHECK_GE((val1), (val2)-(margin));           \
  } while (0)

// perror()..googly style!
//
// PLOG() and PLOG_IF() and PCHECK() behave exactly like their LOG* and
// CHECK equivalents with the addition that they postpend a description
// of the current state of errno to their output lines.

#define PLOG(severity) GOOGLE_PLOG(severity, 0).stream()

#define GOOGLE_PLOG(severity, counter)  \
  @ac_google_namespace@::ErrnoLogMessage( \
      __FILE__, __LINE__, @ac_google_namespace@::GLOG_ ## severity, counter, \
      &@ac_google_namespace@::LogMessage::SendToLog)

#define PLOG_IF(severity, condition) \
  static_cast<void>(0),              \
  !(condition) ? (void) 0 : @ac_google_namespace@::LogMessageVoidify() & PLOG(severity)

// A CHECK() macro that postpends errno if the condition is false. E.g.
//
// if (poll(fds, nfds, timeout) == -1) { PCHECK(errno == EINTR); ... }
#define PCHECK(condition)  PLOG_IF(FATAL, (!(condition))) << "Check failed: " #condition " "

// A CHECK() macro that lets you assert the success of a function that
// returns -1 and sets errno in case of an error. E.g.
//
// CHECK_ERR(mkdir(path, 0700));
//
// or
//
// int fd = open(filename, flags); CHECK_ERR(fd) << ": open " << filename;
#define CHECK_ERR(invocation) PLOG_IF(FATAL, GOOGLE_PREDICT_BRANCH_NOT_TAKEN((invocation) == -1)) << #invocation

//////////////////////////////////////////////////////////////////////////////
    

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
