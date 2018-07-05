/*
 * Logging.h
 *
 * Copyright (c) 2018 by eBay Corporation
 */

#include <cstdio>
#include <memory>
#include <unordered_map>
#include <spdlog/spdlog.h>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>

#pragma once

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

#define LEVELCHECK(l, mod, lvl) (l && sds_logging::module_level_##mod <= (lvl))

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

namespace sds_logging {
template <typename T>
using shared = std::shared_ptr<T>;

extern thread_local shared<spdlog::logger> sds_thread_logger;
extern shared<spdlog::logger> GetLogger() __attribute__((weak));
}

#define MODLEVELDEC(r, _, module) \
    namespace sds_logging { \
        extern spdlog::level::level_enum BOOST_PP_CAT(module_level_, module); \
    }
MODLEVELDEC(_, _, base)

#define MODLEVELDEF(r, l, module) \
    namespace sds_logging { \
        spdlog::level::level_enum BOOST_PP_CAT(module_level_, module) {l}; \
    }

#define SDS_LOGGING_DECL(...)                                                   \
   BOOST_PP_SEQ_FOR_EACH(MODLEVELDEC, spdlog::level::level_enum::off, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

#define SDS_LOGGING_INIT(...)                                                   \
   static std::shared_ptr<spdlog::logger> logger_;                              \
                                                                                \
   BOOST_PP_SEQ_FOR_EACH(MODLEVELDEF, spdlog::level::level_enum::off, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)) \
   namespace sds_logging {                                                      \
   thread_local shared<spdlog::logger> sds_thread_logger;                       \
                                                                                \
   shared<spdlog::logger> GetLogger() {                                         \
       return logger_;                                                          \
   }                                                                            \
                                                                                \
   void SetLogger(shared<spdlog::logger> logger,                                \
                  spdlog::level::level_enum const lvl = spdlog::level::level_enum::off) { \
       if (logger) {                                                            \
            logger->set_level(spdlog::level::level_enum::trace);                \
            module_level_base = lvl;                                            \
       }                                                                        \
       logger_ = logger; sds_thread_logger = logger;                            \
   }                                                                            \
   }

#define SDS_LOG_LEVEL(mod, lvl) BOOST_PP_CAT(sds_logging::module_level_, mod) = (lvl);
