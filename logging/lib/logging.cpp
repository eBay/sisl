/*
 * Logging.cpp
 *
 * Copyright (c) 2018 by eBay Corporation
 */

#include "logging.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
extern "C" {
#include <linux/limits.h>
#include <unistd.h>
}

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
                          (rot_limit,  "",  "logfile_cnt", "Number of rotating files", ::cxxopts::value<uint32_t>()->default_value("5"), "count"), \
                          (size_limit, "",  "logfile_size", "Maximum logfile size", ::cxxopts::value<uint32_t>()->default_value("25"), "MiB"), \
                          (standout,   "c", "stdout", "Stdout logging only", ::cxxopts::value<bool>(), ""), \
                          (quiet,      "q", "quiet", "Disable all console logging", ::cxxopts::value<bool>(), ""), \
                          (synclog,    "s", "synclog", "Synchronized logging", ::cxxopts::value<bool>(), ""), \
                          (flush,      "",  "flush_every", "Flush logs on level (sync mode) or periodically (async mode)", ::cxxopts::value<uint32_t>()->default_value("2"), "level/seconds"), \
                          (verbosity,  "v", "verbosity", "Verbosity filter (0-5)", ::cxxopts::value<std::string>()->default_value("info"), "level"), \
                          (version,    "V", "version", "Print the version and exist", ::cxxopts::value<bool>(), ""))
// clang-format on

namespace sds_logging {

constexpr auto Ki = 1024ul;
constexpr auto Mi = Ki * Ki;

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

static std::filesystem::path log_path(std::string const& name) {
    auto cwd = get_current_dir_name();
    auto p = std::filesystem::path(cwd) / "logs";
    free(cwd);
    if (0 < SDS_OPTIONS.count("logfile")) {
        p = std::filesystem::path(SDS_OPTIONS["logfile"].as< std::string >());
    } else {
        // Construct a unique directory path based on the current time
        auto const current_time = std::chrono::system_clock::now();
        auto const current_t = std::chrono::system_clock::to_time_t(current_time);
        auto const current_tm = std::localtime(&current_t);
        if (char c_time[PATH_MAX] = {'\0'}; strftime(c_time, PATH_MAX, "%F_%R", current_tm)) {
            p /= c_time;
            std::filesystem::create_directories(p);
        }
        p /= std::filesystem::path(name).filename();
    }
    return p;
}

namespace sinks = spdlog::sinks;
template < typename N, typename S >
static void configure_sinks(N const& name, S& sinks, S& crit_sinks) {
    if (!SDS_OPTIONS.count("stdout")) {
        auto const base_path = log_path(name);
        auto const rot_size = SDS_OPTIONS["logfile_size"].as< uint32_t >() * Mi;
        auto const rot_num = SDS_OPTIONS["logfile_cnt"].as< uint32_t >();

        sinks.push_back(
            std::make_shared< sinks::rotating_file_sink_mt >(base_path.string() + "_log", rot_size, rot_num));
        crit_sinks.push_back(
            std::make_shared< sinks::rotating_file_sink_mt >(base_path.string() + "_critical_log", rot_size, rot_num));
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

static void _set_module_log_level(const std::string& module_name, spdlog::level::level_enum level) {
    auto sym = "module_level_" + module_name;
    auto mod_level = (spdlog::level::level_enum*)dlsym(RTLD_DEFAULT, sym.c_str());
    if (mod_level == nullptr) {
        LOGWARN("Unable to locate the module {} in registered modules", module_name);
        return;
    }

    *mod_level = level;
}

static std::string setup_modules() {
    std::string out_str;

    if (SDS_OPTIONS.count("verbosity")) {
        auto lvl_str = SDS_OPTIONS["verbosity"].as< std::string >();
        auto lvl = spdlog::level::from_str(lvl_str);
        if (spdlog::level::level_enum::off == lvl && lvl_str.size() == 1) {
            lvl = (spdlog::level::level_enum)std::stoi(lvl_str);
            lvl_str = spdlog::level::to_string_view(lvl).data();
        }

        for (auto& module_name : glob_enabled_mods) {
            _set_module_log_level(module_name, lvl);
            fmt::format_to(std::back_inserter(out_str), "{}={}, ", module_name, lvl_str);
        }
    } else {
        if (SDS_OPTIONS.count("log_mods")) {
            std::regex re("[\\s,]+");
            auto s = SDS_OPTIONS["log_mods"].as< std::string >();
            std::sregex_token_iterator it(s.begin(), s.end(), re, -1);
            std::sregex_token_iterator reg_end;
            for (; it != reg_end; ++it) {
                auto mod_stream = std::istringstream(it->str());
                std::string module_name, module_level;
                getline(mod_stream, module_name, ':');
                auto sym = "module_level_" + module_name;
                if (auto mod_level = (spdlog::level::level_enum*)dlsym(RTLD_DEFAULT, sym.c_str());
                    nullptr != mod_level) {
                    if (getline(mod_stream, module_level, ':')) {
                        *mod_level = (1 == module_level.size())
                            ? (spdlog::level::level_enum)strtol(module_level.data(), nullptr, 0)
                            : spdlog::level::from_str(module_level.data());
                    }
                } else {
                    LOGWARN("Could not load module logger: {}\n{}", module_name, dlerror());
                }
            }
        }

        for (auto& module_name : glob_enabled_mods) {
            fmt::format_to(std::back_inserter(out_str), "{}={}, ", module_name,
                           spdlog::level::to_string_view(GetModuleLogLevel(module_name)).data());
        }
    }

    return out_str;
}

void SetLogger(std::string const& name, std::string const& pkg, std::string const& ver) {
    std::vector< spdlog::sink_ptr > mysinks{};
    std::vector< spdlog::sink_ptr > critical_sinks{};
    configure_sinks(name, mysinks, critical_sinks);

    set_global_logger(name, mysinks, critical_sinks);

    if (0 == SDS_OPTIONS.count("synclog")) {
        spdlog::flush_every(std::chrono::seconds(SDS_OPTIONS["flush_every"].as< uint32_t >()));
    }

    if (0 < SDS_OPTIONS["version"].count()) {
        spdlog::set_pattern("%v");
        sds_logging::GetLogger()->info("{} - {}", pkg, ver);
        exit(0);
    }

    auto log_details = setup_modules();
    LOGINFO("Logging initialized: {}/{}, [logmods: {}]", pkg, ver, log_details);
}

void SetModuleLogLevel(const std::string& module_name, spdlog::level::level_enum level) {
    _set_module_log_level(module_name, level);
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
    for (auto& mod_name : glob_enabled_mods) {
        j[mod_name] = spdlog::level::to_string_view(GetModuleLogLevel(mod_name)).data();
    }
    return j;
}

void SetAllModuleLogLevel(spdlog::level::level_enum level) {
    for (auto& mod_name : glob_enabled_mods) {
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
