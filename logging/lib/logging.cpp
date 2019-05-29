/*
 * Logging.cpp
 *
 * Copyright (c) 2018 by eBay Corporation
 */

#include "logging.h"

#include <sds_options/options.h>
#include <spdlog/async.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "backtrace.h"

SDS_OPTION_GROUP(logging, (enab_mods,  "", "log_mods", "Module loggers to enable", ::cxxopts::value<std::string>(), "mod[:level][,mod2[:level2],...]"), \
                          (async_size, "", "log_queue", "Size of async log queue", ::cxxopts::value<uint32_t>()->default_value("4096"), "(power of 2)"), \
                          (log_name,   "l", "logfile", "Full path to logfile", ::cxxopts::value<std::string>(), "logfile"), \
                          (rot_limit,  "",  "logfile_cnt", "Number of rotating files", ::cxxopts::value<uint32_t>()->default_value("3"), "count"), \
                          (size_limit, "",  "logfile_size", "Maximum logfile size", ::cxxopts::value<uint32_t>()->default_value("10"), "MiB"), \
                          (standout,   "c", "stdout", "Stdout logging only", ::cxxopts::value<bool>(), ""), \
                          (quiet,      "q", "quiet", "Disable all console logging", ::cxxopts::value<bool>(), ""), \
                          (synclog,    "s", "synclog", "Synchronized logging", ::cxxopts::value<bool>(), ""), \
                          (flush,      "",  "flush_every", "Flush logs on level (sync mode) or periodically (async mode)", ::cxxopts::value<uint32_t>()->default_value("2"), "level/seconds"), \
                          (verbosity,  "v", "verbosity", "Verbosity filter (0-5)", ::cxxopts::value<uint32_t>()->default_value("2"), "level"))

namespace sds_logging {
std::shared_ptr< spdlog::logger >& GetLogger() {
   if (LOGGING_PREDICT_BRANCH_NOT_TAKEN(!(logger_thread_ctx.m_logger))) {
      logger_thread_ctx.m_logger = glob_spdlog_logger;
   }
   return logger_thread_ctx.m_logger;
}

namespace sinks = spdlog::sinks;
void SetLogger(std::string const& name, std::string const& pkg, std::string const& ver) {
   std::vector<spdlog::sink_ptr> mysinks { };
   if (!SDS_OPTIONS.count("stdout")) {
     std::string const path = (0 < SDS_OPTIONS.count("logfile") ?
                                SDS_OPTIONS["logfile"].as<std::string>() :
                                "./" + std::string(file_name(name.c_str())) + "_log");                              \
     auto rotating_sink = std::make_shared<sinks::rotating_file_sink_mt>(path,
                               SDS_OPTIONS["logfile_size"].as<uint32_t>() * (1024 * 1024),
                               SDS_OPTIONS["logfile_cnt"].as<uint32_t>());
     mysinks.push_back(std::move(rotating_sink));
   }
   if (SDS_OPTIONS.count("stdout") || (!SDS_OPTIONS.count("quiet"))) {
      mysinks.push_back(std::make_shared<sinks::stdout_color_sink_mt>());
   }
   if (SDS_OPTIONS.count("synclog")) {
      glob_spdlog_logger = std::make_shared<spdlog::logger>(name, mysinks.begin(), mysinks.end());
      glob_spdlog_logger->flush_on((spdlog::level::level_enum)SDS_OPTIONS["flush_every"].as<uint32_t>());
   } else {
      spdlog::init_thread_pool(SDS_OPTIONS["log_queue"].as<uint32_t>(), 1);
      glob_spdlog_logger = std::make_shared<spdlog::async_logger>(name, mysinks.begin(), mysinks.end(),
                                                                          spdlog::thread_pool());
   }
   glob_spdlog_logger->set_level(spdlog::level::level_enum::trace);
   spdlog::register_logger(glob_spdlog_logger);
   if (0 == SDS_OPTIONS.count("synclog")) {
      spdlog::flush_every(std::chrono::seconds(SDS_OPTIONS["flush_every"].as<uint32_t>()));
   }
   auto lvl = spdlog::level::level_enum::info;
   if (SDS_OPTIONS.count("verbosity")) {
      lvl = (spdlog::level::level_enum)SDS_OPTIONS["verbosity"].as<uint32_t>();
   }
   module_level_base = lvl;
   LOGINFO("Logging initialized [{}]: {}/{}", spdlog::level::to_string_view(lvl), pkg, ver);
   if (SDS_OPTIONS.count("log_mods")) {
      std::vector<std::string> enabled_mods;
      std::regex re("[\\s,]+");
      auto s = SDS_OPTIONS["log_mods"].as<std::string>();
      std::sregex_token_iterator it(s.begin(), s.end(), re, -1);
      std::sregex_token_iterator reg_end;
      for (; it != reg_end; ++it) {
         auto mod_stream = std::istringstream(it->str());
         std::string module_name, module_level;
         getline(mod_stream, module_name, ':');
         auto sym = "module_level_" + module_name;
         if (auto mod_level = (spdlog::level::level_enum*)dlsym(RTLD_DEFAULT,
                                                                sym.c_str());
                   nullptr != mod_level) {
            if (getline(mod_stream, module_level, ':')) {
              *mod_level = (spdlog::level::level_enum)strtol(module_level.data(), nullptr, 0);
            } else {
              *mod_level = lvl;
            }
            enabled_mods.push_back(fmt::format(FMT_STRING("[{}:{}]"),
                                          module_name,
                                          spdlog::level::to_string_view(*mod_level)));
         } else {
            LOGWARN("Could not load module logger: {}\n{}",
                    module_name,
                    dlerror());
         }
      }
      LOGINFO("Enabled modules:\t{}", std::accumulate(enabled_mods.begin(),
                                                         enabled_mods.end(),
                                                         std::string("")));
   }
}

LoggerThreadContext::LoggerThreadContext() {
    m_thread_id = pthread_self();
    LoggerThreadContext::add_logger_thread(this);
}

LoggerThreadContext::~LoggerThreadContext() {
    LoggerThreadContext::remove_logger_thread(this);
}
}
