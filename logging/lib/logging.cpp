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

// clang-format off
SDS_OPTION_GROUP(logging, (enab_mods,  "", "log_mods", "Module loggers to enable", ::cxxopts::value<std::string>(), "mod[:level][,mod2[:level2],...]"), \
                          (async_size, "", "log_queue", "Size of async log queue", ::cxxopts::value<uint32_t>()->default_value("4096"), "(power of 2)"), \
                          (log_name,   "l", "logfile", "Full path to logfile", ::cxxopts::value<std::string>(), "logfile"), \
                          (rot_limit,  "",  "logfile_cnt", "Number of rotating files", ::cxxopts::value<uint32_t>()->default_value("3"), "count"), \
                          (size_limit, "",  "logfile_size", "Maximum logfile size", ::cxxopts::value<uint32_t>()->default_value("10"), "MiB"), \
                          (standout,   "c", "stdout", "Stdout logging only", ::cxxopts::value<bool>(), ""), \
                          (quiet,      "q", "quiet", "Disable all console logging", ::cxxopts::value<bool>(), ""), \
                          (synclog,    "s", "synclog", "Synchronized logging", ::cxxopts::value<bool>(), ""), \
                          (flush,      "",  "flush_every", "Flush logs on level (sync mode) or periodically (async mode)", ::cxxopts::value<uint32_t>()->default_value("2"), "level/seconds"), \
                          (verbosity,  "v", "verbosity", "Verbosity filter (0-5)", ::cxxopts::value<uint32_t>()->default_value("2"), "level"), \
                          (version,    "V", "version", "Print the version and exist", ::cxxopts::value<bool>(), ""))
// clang-format on

namespace sds_logging {
std::shared_ptr< spdlog::logger >& GetLogger() {
    if (LOGGING_PREDICT_BRANCH_NOT_TAKEN(!(logger_thread_ctx.m_logger))) {
        logger_thread_ctx.m_logger = glob_spdlog_logger;
    }
    return logger_thread_ctx.m_logger;
}

std::shared_ptr< spdlog::logger >& GetCriticalLogger() {
    if (LOGGING_PREDICT_BRANCH_NOT_TAKEN(!(logger_thread_ctx.m_critical_logger))) {
        logger_thread_ctx.m_critical_logger = glob_critical_logger;
    }
    return logger_thread_ctx.m_critical_logger;
}

namespace sinks = spdlog::sinks;
template < typename N, typename S >
static void configure_sinks(N const& name, S& sinks, S& crit_sinks) {
    if (!SDS_OPTIONS.count("stdout")) {
        std::string const path =
            (0 < SDS_OPTIONS.count("logfile") ? SDS_OPTIONS["logfile"].as< std::string >()
                                              : "./" + std::string(file_name(name.c_str())) + "_log");
        auto rotating_sink = std::make_shared< sinks::rotating_file_sink_mt >(
            path, SDS_OPTIONS["logfile_size"].as< uint32_t >() * (1024 * 1024),
            SDS_OPTIONS["logfile_cnt"].as< uint32_t >());
        sinks.push_back(std::move(rotating_sink));

        // Create a separate sink for critical logs
        std::string const crit_logpath =
            (0 < SDS_OPTIONS.count("logfile") ? SDS_OPTIONS["logfile"].as< std::string >() + "_critical_log"
                                              : "./" + std::string(file_name(name.c_str())) + "_critical_log");
        auto critical_sink = std::make_shared< sinks::rotating_file_sink_mt >(
            crit_logpath, SDS_OPTIONS["logfile_size"].as< uint32_t >() * (1024 * 1024),
            SDS_OPTIONS["logfile_cnt"].as< uint32_t >());
        crit_sinks.push_back(std::move(critical_sink));
    }

    if (SDS_OPTIONS.count("stdout") || (!SDS_OPTIONS.count("quiet"))) {
        sinks.push_back(std::make_shared< sinks::stdout_color_sink_mt >());
    }
}

template < typename N, typename S >
void set_global_logger(N const& name, S const& sinks, S const& crit_sinks) {
    // Create/Setup and register spdlog regular logger
    if (SDS_OPTIONS.count("synclog")) {
        glob_spdlog_logger = std::make_shared< spdlog::logger >(name, sinks.begin(), sinks.end());
        glob_spdlog_logger->flush_on((spdlog::level::level_enum)SDS_OPTIONS["flush_every"].as< uint32_t >());
    } else {
        spdlog::init_thread_pool(SDS_OPTIONS["log_queue"].as< uint32_t >(), 1);
        glob_spdlog_logger =
            std::make_shared< spdlog::async_logger >(name, sinks.begin(), sinks.end(), spdlog::thread_pool());
    }
    glob_spdlog_logger->set_level(spdlog::level::level_enum::trace);
    spdlog::register_logger(glob_spdlog_logger);
    //
    // Create/Setup and register critical logger. Critical logger is sync logger
    glob_critical_logger = std::make_shared< spdlog::logger >(name + "_critical", crit_sinks.begin(), crit_sinks.end());
    glob_critical_logger->flush_on(spdlog::level::err);
    glob_critical_logger->set_level(spdlog::level::level_enum::err);
    spdlog::register_logger(glob_critical_logger);
}

static void setup_modules(spdlog::level::level_enum const lvl) {
    module_level_base = lvl;
    if (SDS_OPTIONS.count("log_mods")) {
        std::regex                 re("[\\s,]+");
        auto                       s = SDS_OPTIONS["log_mods"].as< std::string >();
        std::sregex_token_iterator it(s.begin(), s.end(), re, -1);
        std::sregex_token_iterator reg_end;
        for (; it != reg_end; ++it) {
            auto        mod_stream = std::istringstream(it->str());
            std::string module_name, module_level;
            getline(mod_stream, module_name, ':');
            auto sym = "module_level_" + module_name;
            if (auto mod_level = (spdlog::level::level_enum*)dlsym(RTLD_DEFAULT, sym.c_str()); nullptr != mod_level) {
                if (getline(mod_stream, module_level, ':')) {
                    *mod_level = (spdlog::level::level_enum)strtol(module_level.data(), nullptr, 0);
                } else {
                    *mod_level = lvl;
                }
                glob_enabled_mods.push_back(module_name);
            } else {
                LOGWARN("Could not load module logger: {}\n{}", module_name, dlerror());
            }
        }
        if (0 < glob_enabled_mods.size()) {
            auto const dash_fold = [](std::string a, std::string b) { return std::move(a) + ", " + b; };
            LOGINFO("Enabled modules:\t{}",
                    std::accumulate(std::next(glob_enabled_mods.begin()), glob_enabled_mods.end(), glob_enabled_mods[0],
                                    dash_fold));
        }
    }
}

void SetLogger(std::string const& name, std::string const& pkg, std::string const& ver) {
    std::vector< spdlog::sink_ptr > mysinks{};
    std::vector< spdlog::sink_ptr > critical_sinks{};
    configure_sinks(name, mysinks, critical_sinks);

    set_global_logger(name, mysinks, critical_sinks);

    if (0 == SDS_OPTIONS.count("synclog")) {
        spdlog::flush_every(std::chrono::seconds(SDS_OPTIONS["flush_every"].as< uint32_t >()));
    }

    auto lvl = spdlog::level::level_enum::info;
    if (SDS_OPTIONS.count("verbosity")) {
        lvl = (spdlog::level::level_enum)SDS_OPTIONS["verbosity"].as< uint32_t >();
    }

    if (0 < SDS_OPTIONS["version"].count()) {
        spdlog::set_pattern("%v");
        sds_logging::GetLogger()->info("{} - {}", pkg, ver);
        exit(0);
    }

    LOGINFO("Logging initialized [{}]: {}/{}", spdlog::level::to_string_view(lvl), pkg, ver);
    setup_modules(lvl);
}

void SetModuleLogLevel(const std::string& module_name, spdlog::level::level_enum level) {
    auto sym = "module_level_" + module_name;
    auto mod_level = (spdlog::level::level_enum*)dlsym(RTLD_DEFAULT, sym.c_str());
    if (mod_level == nullptr) {
        LOGWARN("Unable to locate the module {} in registered modules", module_name);
        return;
    }

    *mod_level = level;
    LOGINFO("Set module '{}' log level to '{}'", module_name, spdlog::level::to_string_view(level));
}

spdlog::level::level_enum GetModuleLogLevel(const std::string& module_name) {
    auto sym = "module_level_" + module_name;
    auto mod_level = (spdlog::level::level_enum*)dlsym(RTLD_DEFAULT, sym.c_str());
    if (mod_level == nullptr) {
        LOGWARN("Unable to locate the module {} in registered modules", module_name);
        return spdlog::level::level_enum::off;
    }

    return *mod_level;
}

nlohmann::json GetAllModuleLogLevel() {
    nlohmann::json j;
    for (auto mod_name : glob_enabled_mods) {
        j[mod_name] = spdlog::level::to_string_view(GetModuleLogLevel(mod_name)).data();
    }
    return j;
}

void SetAllModuleLogLevel(spdlog::level::level_enum level) {
    for (auto mod_name : glob_enabled_mods) {
        SetModuleLogLevel(mod_name, level);
    }
}

std::string format_log_msg() { return ""; }

LoggerThreadContext::LoggerThreadContext() {
    m_thread_id = pthread_self();
    LoggerThreadContext::add_logger_thread(this);
}

LoggerThreadContext::~LoggerThreadContext() { LoggerThreadContext::remove_logger_thread(this); }
} // namespace sds_logging
