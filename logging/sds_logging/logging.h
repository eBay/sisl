/*
 * Logging.h
 *
 * Copyright (c) 2018 by eBay Corporation
 */

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
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/logger.h>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/variadic/to_tuple.hpp>

#include <sds_options/options.h>

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

namespace sds_logging {
template <typename T>
using shared = std::shared_ptr<T>;

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
   SDS_OPTION_GROUP(logging, (enab_mods,  "", "log_mods", "Module loggers to enable", ::cxxopts::value<std::string>(), "mod[:level][,mod2[:level2],...]"), \
                             (async_size, "", "log_queue", "Size of async log queue", ::cxxopts::value<uint32_t>()->default_value("4096"), "(power of 2)"), \
                             (log_name,   "l", "logfile", "Full path to logfile", ::cxxopts::value<std::string>(), "logfile"), \
                             (rot_limit,  "",  "logfile_cnt", "Number of rotating files", ::cxxopts::value<uint32_t>()->default_value("3"), "count"), \
                             (size_limit, "",  "logfile_size", "Maximum logfile size", ::cxxopts::value<uint32_t>()->default_value("10"), "MiB"), \
                             (standout,   "c", "stdout", "Stdout logging only", ::cxxopts::value<bool>(), ""), \
                             (quiet,      "q", "quiet", "Disable all console logging", ::cxxopts::value<bool>(), ""), \
                             (synclog,    "s", "synclog", "Synchronized logging", ::cxxopts::value<bool>(), ""), \
                             (verbosity,  "v", "verbosity", "Verbosity filter (0-5)", ::cxxopts::value<uint32_t>()->default_value("2"), "level")) \
   static sds_logging::shared<spdlog::logger> logger_;                                  \
                                                                                        \
   BOOST_PP_SEQ_FOR_EACH(MODLEVELDEF, spdlog::level::level_enum::critical, BOOST_PP_TUPLE_TO_SEQ(BOOST_PP_TUPLE_PUSH_FRONT(BOOST_PP_VARIADIC_TO_TUPLE(__VA_ARGS__), base))) \
   namespace sds_logging {                                                              \
   thread_local shared<spdlog::logger> sds_thread_logger;                               \
                                                                                        \
   shared<spdlog::logger> GetLogger() {                                                 \
       return logger_;                                                                  \
   }                                                                                    \
                                                                                        \
   namespace sinks = spdlog::sinks;                                                     \
   void SetLogger(std::string const& name) {                                            \
       std::vector<spdlog::sink_ptr> mysinks { };                                       \
       if (!SDS_OPTIONS.count("stdout")) {                                              \
         std::string const path = (0 < SDS_OPTIONS.count("logfile") ?                   \
                                    SDS_OPTIONS["logfile"].as<std::string>() :          \
                                    "./" + std::string(file_name(name.c_str())) + "_log");                              \
         auto rotating_sink = std::make_shared<sinks::rotating_file_sink_mt>(path,      \
                                   SDS_OPTIONS["logfile_size"].as<uint32_t>() * (1024 * 1024), \
                                   SDS_OPTIONS["logfile_cnt"].as<uint32_t>());          \
         mysinks.push_back(std::move(rotating_sink));                                   \
       }                                                                                \
       if (SDS_OPTIONS.count("stdout") || (!SDS_OPTIONS.count("quiet"))) {              \
          mysinks.push_back(std::make_shared<sinks::stdout_color_sink_mt>());           \
       }                                                                                \
       if (SDS_OPTIONS.count("synclog") && SDS_OPTIONS.count("stdout")) {               \
          logger_ = std::make_shared<spdlog::logger>(name,                              \
                                                     mysinks.begin(),                   \
                                                     mysinks.end());                    \
       } else {                                                                         \
          spdlog::init_thread_pool(SDS_OPTIONS["log_queue"].as<uint32_t>(), 1);         \
          logger_ = std::make_shared<spdlog::async_logger>(name,                        \
                                                           mysinks.begin(),             \
                                                           mysinks.end(),               \
                                                           spdlog::thread_pool());      \
       }                                                                                \
       logger_->set_level(spdlog::level::level_enum::trace);                            \
       spdlog::register_logger(logger_);                                                \
       auto lvl = spdlog::level::level_enum::info;                                      \
       if (SDS_OPTIONS.count("verbosity")) {                                            \
          lvl = (spdlog::level::level_enum)SDS_OPTIONS["verbosity"].as<uint32_t>();     \
       }                                                                                \
       module_level_base = lvl;                                                         \
       sds_thread_logger = logger_;                                                     \
       LOGINFO("Logging initialized [{}]: {}/{}",                                       \
               spdlog::level::to_c_str(lvl),                                            \
               BOOST_PP_STRINGIZE(PACKAGE_NAME),                                        \
               BOOST_PP_STRINGIZE(PACKAGE_VERSION));                                    \
       if (SDS_OPTIONS.count("log_mods")) {                                             \
          std::vector<std::string> enabled_mods;                                        \
          std::regex re("[\\s,]+");                                                     \
          auto s = SDS_OPTIONS["log_mods"].as<std::string>();                           \
          std::sregex_token_iterator it(s.begin(), s.end(), re, -1);                    \
          std::sregex_token_iterator reg_end;                                           \
          for (; it != reg_end; ++it) {                                                 \
             auto mod_stream = std::istringstream(it->str());                           \
             std::string module_name, module_level;                                     \
             getline(mod_stream, module_name, ':');                                     \
             auto sym = "module_level_" + module_name;                                  \
             if (auto mod_level = (spdlog::level::level_enum*)dlsym(RTLD_DEFAULT,       \
                                                                    sym.c_str());       \
                       nullptr != mod_level) {                                          \
                if (getline(mod_stream, module_level, ':')) {                           \
                  *mod_level = (spdlog::level::level_enum)atoi(module_level.data());    \
                } else {                                                                \
                  *mod_level = lvl;                                                     \
                }                                                                       \
                enabled_mods.push_back(format(fmt("[{}:{}]"),                           \
                                              module_name,                              \
                                              spdlog::level::to_c_str(*mod_level)));    \
             } else {                                                                   \
                LOGWARN("Could not load module logger: {}\n{}",                         \
                        module_name,                                                    \
                        dlerror());                                                     \
             }                                                                          \
          }                                                                             \
          LOGINFO("Enabled modules:\t{}", std::accumulate(enabled_mods.begin(),         \
                                                             enabled_mods.end(),        \
                                                             std::string("")));         \
       }                                                                                \
   }                                                                                    \
   }

#define SDS_LOG_LEVEL(mod, lvl) BOOST_PP_CAT(module_level_, mod) = (lvl);
