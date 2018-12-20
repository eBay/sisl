/*
 * Logging.cpp
 *
 * Copyright (c) 2018 by eBay Corporation
 */

#include "logging.h"

namespace sds_logging {
thread_local shared<spdlog::logger> sds_thread_logger;

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
   if (SDS_OPTIONS.count("synclog") && SDS_OPTIONS.count("stdout")) {
      logger_ = std::make_shared<spdlog::logger>(name,
                                                 mysinks.begin(),
                                                 mysinks.end());
   } else {
      spdlog::init_thread_pool(SDS_OPTIONS["log_queue"].as<uint32_t>(), 1);
      logger_ = std::make_shared<spdlog::async_logger>(name,
                                                       mysinks.begin(),
                                                       mysinks.end(),
                                                       spdlog::thread_pool());
   }
   logger_->set_level(spdlog::level::level_enum::trace);
   spdlog::register_logger(logger_);
   auto lvl = spdlog::level::level_enum::info;
   if (SDS_OPTIONS.count("verbosity")) {
      lvl = (spdlog::level::level_enum)SDS_OPTIONS["verbosity"].as<uint32_t>();
   }
   module_level_base = lvl;
   sds_thread_logger = logger_;
   LOGINFO("Logging initialized [{}]: {}/{}", spdlog::level::to_c_str(lvl), pkg, ver);
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
                                          spdlog::level::to_c_str(*mod_level)));
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
}
