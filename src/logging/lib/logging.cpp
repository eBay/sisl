/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Brian Szymd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/

#include "logging.h"

#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iterator>
#include <iomanip>
#include <regex>

#if defined(__linux__) || defined(__APPLE__)
#if defined(__APPLE__)
#undef _POSIX_C_SOURCE
#endif
#include <dlfcn.h>
#include <unistd.h>
#endif

#if defined(__linux__)
#include <linux/limits.h>
#endif

#include "options/options.h"
#include <spdlog/async.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "backtrace.h"

// clang-format off
SISL_OPTION_GROUP(logging, (enab_mods,  "", "log_mods", "Module loggers to enable", ::cxxopts::value<std::string>(), "mod[:level][,mod2[:level2],...]"), \
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

// logger required define if not inited
extern "C" {
spdlog::level::level_enum module_level_base{spdlog::level::level_enum::info};
}

namespace sisl {
namespace logging {

constexpr uint64_t Ki{1024};
constexpr uint64_t Mi{Ki * Ki};

// SISL_LOGGING_INIT declared global variables
static std::shared_ptr< spdlog::logger > glob_spdlog_logger;
static std::shared_ptr< spdlog::logger > glob_critical_logger;
// NOTE: glob_enabled_mods should be a vector but sanitizer reports a leak so changed to array of pointers
static constexpr size_t MAX_MODULES{100};
static std::array< const char*, MAX_MODULES > glob_enabled_mods{"base"};
static size_t glob_num_mods{1};

/****************************** LoggerThreadContext ******************************/
std::mutex LoggerThreadContext::s_logger_thread_mutex;
std::unordered_set< LoggerThreadContext* > LoggerThreadContext::s_logger_thread_set;

LoggerThreadContext::LoggerThreadContext() {
    m_thread_id = pthread_self();
    LoggerThreadContext::add_logger_thread(this);
}

LoggerThreadContext::~LoggerThreadContext() { LoggerThreadContext::remove_logger_thread(this); }

LoggerThreadContext& LoggerThreadContext::instance() {
    static thread_local LoggerThreadContext inst{};
    return inst;
}

void LoggerThreadContext::add_logger_thread(LoggerThreadContext* const ctx) {
    std::unique_lock l{s_logger_thread_mutex};
    s_logger_thread_set.insert(ctx);
}

void LoggerThreadContext::remove_logger_thread(LoggerThreadContext* const ctx) {
    std::unique_lock l{s_logger_thread_mutex};
    s_logger_thread_set.erase(ctx);
}

/******************************** InitModules *********************************/
void InitModules::init_modules(std::initializer_list< const char* > mods_list) {
    assert(glob_num_mods + mods_list.size() <= MAX_MODULES);
    for (const auto& mod : mods_list) {
        glob_enabled_mods[glob_num_mods++] = mod;
    }
}

std::shared_ptr< spdlog::logger >& GetLogger() {
#if __cplusplus > 201703L
    [[unlikely]] if (!(logger_thread_ctx.m_logger)) {
#else
    if (LOGGING_PREDICT_FALSE(!(logger_thread_ctx.m_logger))) {
#endif
        logger_thread_ctx.m_logger = glob_spdlog_logger;
    }
    return logger_thread_ctx.m_logger;
}

std::shared_ptr< spdlog::logger >& GetCriticalLogger() {
#if __cplusplus > 201703L
    [[unlikely]] if (!(logger_thread_ctx.m_critical_logger)) {
#else
    if (LOGGING_PREDICT_FALSE(!(logger_thread_ctx.m_critical_logger))) {
#endif
        logger_thread_ctx.m_critical_logger = glob_critical_logger;
    }
    return logger_thread_ctx.m_critical_logger;
}

static std::filesystem::path get_base_dir() {
    const auto cwd{std::filesystem::current_path()};
    auto p{cwd / "logs"};
    // Construct a unique directory path based on the current time
    auto const current_time{std::chrono::system_clock::now()};
    auto const current_t{std::chrono::system_clock::to_time_t(current_time)};
    auto const current_tm{std::localtime(&current_t)};
    std::array< char, PATH_MAX > c_time;
    if (std::strftime(c_time.data(), c_time.size(), "%F_%R", current_tm)) {
        p /= c_time.data();
        std::filesystem::create_directories(p);
    }
    return p;
}

static std::filesystem::path log_path(std::string const& name) {
    std::filesystem::path p;
    if (0 < SISL_OPTIONS.count("logfile")) {
        p = std::filesystem::path{SISL_OPTIONS["logfile"].as< std::string >()};
    } else {
        static std::filesystem::path base_dir{get_base_dir()};
        p = base_dir / std::filesystem::path{name}.filename();
    }
    return p;
}

namespace sinks = spdlog::sinks;

template < typename N, typename S >
static void configure_sinks(N const& name, S& sinks, S& crit_sinks) {
    if (!SISL_OPTIONS.count("stdout")) {
        auto const base_path{log_path(name)};
        auto const rot_size{SISL_OPTIONS["logfile_size"].as< uint32_t >() * Mi};
        auto const rot_num{SISL_OPTIONS["logfile_cnt"].as< uint32_t >()};

        sinks.push_back(
            std::make_shared< sinks::rotating_file_sink_mt >(base_path.string() + "_log", rot_size, rot_num));
        crit_sinks.push_back(
            std::make_shared< sinks::rotating_file_sink_mt >(base_path.string() + "_critical_log", rot_size, rot_num));
    }

    if (SISL_OPTIONS.count("stdout") || (!SISL_OPTIONS.count("quiet"))) {
        sinks.push_back(std::make_shared< sinks::stdout_color_sink_mt >());
    }
}

template < typename N, typename S >
static void create_append_sink(N const& name, S& sinks, const std::string& extn, const bool stdout_sink) {
    if (stdout_sink) {
        sinks.push_back(std::make_shared< sinks::stdout_color_sink_mt >());
    } else {
        auto const base_path{log_path(name)};
        auto const rot_size{SISL_OPTIONS["logfile_size"].as< uint32_t >() * Mi};
        auto const rot_num{SISL_OPTIONS["logfile_cnt"].as< uint32_t >()};

        sinks.push_back(
            std::make_shared< sinks::rotating_file_sink_mt >(base_path.string() + extn + "_log", rot_size, rot_num));
    }
}

template < typename N, typename S >
void set_global_logger(N const& name, S const& sinks, S const& crit_sinks) {
    // Create/Setup and register spdlog regular logger
    if (SISL_OPTIONS.count("synclog")) {
        glob_spdlog_logger = std::make_shared< spdlog::logger >(name, sinks.begin(), sinks.end());
        glob_spdlog_logger->flush_on(
            static_cast< spdlog::level::level_enum >(SISL_OPTIONS["flush_every"].as< uint32_t >()));
    } else {
        spdlog::init_thread_pool(SISL_OPTIONS["log_queue"].as< uint32_t >(), 1);
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

static spdlog::level::level_enum* to_mod_log_level_ptr(const std::string& module_name) {
    const auto sym = std::string{"module_level_"} + module_name;
    auto* mod_level = static_cast< spdlog::level::level_enum* >(::dlsym(RTLD_DEFAULT, sym.c_str()));
    if (mod_level == nullptr) {
        std::cout << fmt::format("Unable to locate the module {} in registered modules, error: {}\n", module_name,
                                 dlerror());
    }
    return mod_level;
}

static void set_module_log_level(const std::string& module_name, const spdlog::level::level_enum level) {
    auto* mod_level = to_mod_log_level_ptr(module_name);
    if (mod_level != nullptr) { *mod_level = level; }
}

static std::string setup_modules() {
    std::string out_str;

    if (SISL_OPTIONS.count("verbosity")) {
        auto lvl_str{SISL_OPTIONS["verbosity"].as< std::string >()};
        auto lvl{spdlog::level::from_str(lvl_str)};
        if ((spdlog::level::level_enum::off == lvl) && (lvl_str.size() == 1)) {
            lvl = static_cast< spdlog::level::level_enum >(std::stoi(lvl_str));
            lvl_str = spdlog::level::to_string_view(lvl).data();
        }

        for (size_t mod_num{0}; mod_num < glob_num_mods; ++mod_num) {
            const std::string& mod_name{glob_enabled_mods[mod_num]};
            set_module_log_level(mod_name, lvl);
            fmt::vformat_to(std::back_inserter(out_str), fmt::string_view{"{}={}, "},
                            fmt::make_format_args(mod_name, lvl_str));
        }
    } else {
        if (SISL_OPTIONS.count("log_mods")) {
            std::regex re{"[\\s,]+"};
            const auto s{SISL_OPTIONS["log_mods"].as< std::string >()};
            std::sregex_token_iterator it{std::cbegin(s), std::cend(s), re, -1};
            std::sregex_token_iterator reg_end;
            for (; it != reg_end; ++it) {
                auto mod_stream{std::istringstream(it->str())};
                std::string module_name, module_level;
                std::getline(mod_stream, module_name, ':');
                const auto sym{std::string{"module_level_"} + module_name};
                if (auto* const mod_level{
                        static_cast< spdlog::level::level_enum* >(::dlsym(RTLD_DEFAULT, sym.c_str()))};
                    nullptr != mod_level) {
                    if (std::getline(mod_stream, module_level, ':')) {
                        *mod_level = (1 == module_level.size())
                            ? static_cast< spdlog::level::level_enum >(std::strtol(module_level.data(), nullptr, 0))
                            : spdlog::level::from_str(module_level.data());
                    }
                } else {
                    LOGWARN("Could not load module logger: {}\n{}", module_name, dlerror());
                }
            }
        }

        for (size_t mod_num{0}; mod_num < glob_num_mods; ++mod_num) {
            const std::string& mod_name{glob_enabled_mods[mod_num]};
            fmt::vformat_to(
                std::back_inserter(out_str), fmt::string_view{"{}={}, "},
                fmt::make_format_args(mod_name, spdlog::level::to_string_view(GetModuleLogLevel(mod_name)).data()));
        }
    }

    return out_str;
}

void SetLogger(std::string const& name, std::string const& pkg, std::string const& ver) {
    std::vector< spdlog::sink_ptr > mysinks{};
    std::vector< spdlog::sink_ptr > critical_sinks{};

    // Create set of needed sinks
    if (!SISL_OPTIONS.count("stdout")) {
        create_append_sink(name, mysinks, "", false /* stdout_sink */);
        create_append_sink(name, critical_sinks, "_critical", false /* stdout_sink */);
    }
    if (SISL_OPTIONS.count("stdout") || (!SISL_OPTIONS.count("quiet"))) {
        create_append_sink(name, mysinks, "", true /* stdout_sink */);
    }

    set_global_logger(name, mysinks, critical_sinks);

    if (0 == SISL_OPTIONS.count("synclog")) {
        spdlog::flush_every(std::chrono::seconds(SISL_OPTIONS["flush_every"].as< uint32_t >()));
    }

    if (0 < SISL_OPTIONS["version"].count()) {
        spdlog::set_pattern("%v");
        sisl::logging::GetLogger()->info("{} - {}", pkg, ver);
        std::exit(0);
    }

    const auto log_details{setup_modules()};
    LOGINFO("Logging initialized: {}/{}, [logmods: {}]", pkg, ver, log_details);
}

void SetLogPattern(const std::string& pattern, const std::shared_ptr< logger_t >& logger) {
    if (logger == nullptr) {
        spdlog::set_pattern(pattern);
    } else {
        logger->set_pattern(pattern);
    }
}

std::shared_ptr< logger_t > CreateCustomLogger(const std::string& name, const std::string& extn,
                                               const bool tee_to_stdout, const bool tee_to_stderr) {
    std::vector< spdlog::sink_ptr > sinks{};
    std::shared_ptr< spdlog::logger > custom_logger;

    if (!SISL_OPTIONS.count("stdout")) { create_append_sink(name, sinks, extn, false /* is_stdout_sink */); }
    if ((SISL_OPTIONS.count("stdout") && !tee_to_stderr) || tee_to_stdout) {
        create_append_sink(name, sinks, "", true /* is_stdout_sink */);
    }
    if (!SISL_OPTIONS.count("quiet") && tee_to_stderr) {
        sinks.push_back(std::make_shared< sinks::stderr_color_sink_mt >());
    }

    if (SISL_OPTIONS.count("synclog")) {
        custom_logger = std::make_shared< spdlog::logger >(name, std::begin(sinks), std::end(sinks));
        custom_logger->flush_on((spdlog::level::level_enum)SISL_OPTIONS["flush_every"].as< uint32_t >());
    } else {
        custom_logger =
            std::make_shared< spdlog::async_logger >(name, sinks.begin(), sinks.end(), spdlog::thread_pool());
    }
    custom_logger->set_level(spdlog::level::level_enum::trace);
    spdlog::register_logger(custom_logger);
    return custom_logger;
}

void SetModuleLogLevel(const std::string& module_name, const spdlog::level::level_enum level) {
    set_module_log_level(module_name, level);
    LOGINFO("Set module '{}' log level to '{}'", module_name, spdlog::level::to_string_view(level));
}

spdlog::level::level_enum GetModuleLogLevel(const std::string& module_name) {
    auto* mod_level = to_mod_log_level_ptr(module_name);
    return mod_level ? *mod_level : spdlog::level::level_enum::off;
}

nlohmann::json GetAllModuleLogLevel() {
    nlohmann::json j;
    for (size_t mod_num{0}; mod_num < glob_num_mods; ++mod_num) {
        const std::string& mod_name{glob_enabled_mods[mod_num]};
        j[mod_name] = spdlog::level::to_string_view(GetModuleLogLevel(mod_name)).data();
    }
    return j;
}

void SetAllModuleLogLevel(const spdlog::level::level_enum level) {
    for (size_t mod_num{0}; mod_num < glob_num_mods; ++mod_num) {
        const std::string& mod_name{glob_enabled_mods[mod_num]};
        SetModuleLogLevel(mod_name, level);
    }
}

std::string format_log_msg() { return std::string{}; }
} // namespace logging
} // namespace sisl
