/*
 * Logging.h
 *
 * Copyright (c) 2018 by eBay Corporation
 */

#include <cstdio>
#include <memory>
#include <unordered_map>
#include <spdlog/spdlog.h>

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


#define LINEOUTPUTFORMAT "[{}:{}:{}] "
#define LINEOUTPUTARGS file_name(__FILE__), __LINE__, __FUNCTION__

#define LEVELCHECK(l, lvl) (l && l->should_log(lvl))

#define LOGGER (sds_logging::sds_thread_logger ? sds_logging::sds_thread_logger : sds_logging::sds_thread_logger = sds_logging::GetLogger())

#define LOGTRACE(msg, ...)      if (auto _l = LOGGER; _l && LEVELCHECK(_l, spdlog::level::level_enum::trace)) _l->trace(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGDEBUG(msg, ...)      if (auto _l = LOGGER; _l && LEVELCHECK(_l, spdlog::level::level_enum::debug)) _l->debug(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGINFO(msg, ...)       if (auto _l = LOGGER; _l && LEVELCHECK(_l, spdlog::level::level_enum::info)) _l->info(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGWARN(msg, ...)       if (auto _l = LOGGER; _l && LEVELCHECK(_l, spdlog::level::level_enum::warn)) _l->warn(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGERROR(msg, ...)      if (auto _l = LOGGER; _l && LEVELCHECK(_l, spdlog::level::level_enum::err)) _l->error(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGCRITICAL(msg, ...)   if (auto _l = LOGGER; _l && LEVELCHECK(_l, spdlog::level::level_enum::critical)) _l->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)

#define MODSTRINGIFY(X) MODSTRINGY(X)
#define MODSTRINGY(X) #X
#define MODLOGGER(mod, lvl) sds_logging::GetLogger(MODSTRINGIFY(mod), lvl)

#define LOGTRACEMOD(mod, msg, ...)     if (auto _l = MODLOGGER(mod,spdlog::level::level_enum::trace); _l) _l->trace(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGDEBUGMOD(mod, msg, ...)     if (auto _l = MODLOGGER(mod,spdlog::level::level_enum::debug); _l) _l->debug(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGINFOMOD(mod, msg, ...)      if (auto _l = MODLOGGER(mod,spdlog::level::level_enum::info); _l) _l->info(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGWARNMOD(mod, msg, ...)      if (auto _l = MODLOGGER(mod,spdlog::level::level_enum::warn); _l) _l->warn(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGERRORMOD(mod, msg, ...)     if (auto _l = MODLOGGER(mod,spdlog::level::level_enum::err); _l) _l->error(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGCRITICALMOD(mod, msg, ...)  if (auto _l = MODLOGGER(mod,spdlog::level::level_enum::critical); _l) _l->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)

namespace sds_logging {
template <typename T>
using shared = std::shared_ptr<T>;

extern thread_local shared<spdlog::logger> sds_thread_logger;
extern shared<spdlog::logger> GetLogger() __attribute__((weak));
extern shared<spdlog::logger> GetLogger(std::string const& name, spdlog::level::level_enum const lvl) __attribute__((weak));
extern void EnableModule(std::string const& name) __attribute__((weak));
extern void SetLogger(shared<spdlog::logger>) __attribute__((weak));
}

#define SDS_LOGGING_INIT                                            \
   static std::shared_ptr<spdlog::logger> logger_;                  \
                                                                    \
   namespace sds_logging {                                          \
   thread_local shared<spdlog::logger> sds_thread_logger;           \
   thread_local std::unordered_map<std::string,                     \
                            shared<spdlog::logger>> module_loggers; \
                                                                    \
   void EnableModule(std::string const& n) {                        \
       assert(logger_);                                             \
       auto const& sinks = logger_->sinks();                        \
       auto l = std::make_shared<spdlog::logger>(n,                 \
                                                 sinks.begin(),     \
                                                 sinks.end());      \
       l->set_level(logger_->level());                              \
       spdlog::register_logger(l);                                  \
   }                                                                \
                                                                    \
   shared<spdlog::logger> GetLogger() {                             \
       return logger_;                                              \
   }                                                                \
                                                                    \
   shared<spdlog::logger> GetLogger(std::string const& name,        \
                                    spdlog::level::level_enum const lvl) { \
       if (!LEVELCHECK(LOGGER, lvl)) return nullptr;                \
       if (auto it = module_loggers.find(name);                     \
              module_loggers.end() != it) {                         \
           return it->second;                                       \
       }                                                            \
       auto logger = spdlog::get(name) ;                            \
       module_loggers.emplace(std::make_pair(name, logger));        \
       return logger;                                               \
   }                                                                \
                                                                    \
   void SetLogger(std::shared_ptr<spdlog::logger> logger) {         \
       logger_ = logger; sds_thread_logger = logger;                \
   }                                                                \
   }

#define LOGENABLEMOD(module) sds_logging::EnableModule(MODSTRINGIFY(module));