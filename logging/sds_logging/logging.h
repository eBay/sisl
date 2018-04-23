/*
 * Logging.h
 *
 * Copyright (c) 2018 by eBay Corporation
 */

#include <cstdio>
#include <memory>
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

#define LOGGER sds_logging::GetLogger()

#define LINEOUTPUTFORMAT "[{}:{}:{}] "
#define LINEOUTPUTARGS file_name(__FILE__), __LINE__, __FUNCTION__

#define LEVELCHECK(lvl) if (LOGGER->should_log(lvl))
#define LOGTRACE(msg, ...)     if (auto l = LOGGER) LEVELCHECK(spdlog::level::level_enum::trace) l->trace(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGDEBUG(msg, ...)     if (auto l = LOGGER) LEVELCHECK(spdlog::level::level_enum::debug) l->debug(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGINFO(msg, ...)      if (auto l = LOGGER) LEVELCHECK(spdlog::level::level_enum::info) l->info(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGWARN(msg, ...)      if (auto l = LOGGER) LEVELCHECK(spdlog::level::level_enum::warn) l->warn(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGERROR(msg, ...)     if (auto l = LOGGER) LEVELCHECK(spdlog::level::level_enum::err) l->error(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)
#define LOGCRITICAL(msg, ...)  if (auto l = LOGGER) LEVELCHECK(spdlog::level::level_enum::critical) l->critical(LINEOUTPUTFORMAT msg, LINEOUTPUTARGS, ##__VA_ARGS__)

namespace sds_logging {
extern std::shared_ptr<spdlog::logger> GetLogger() __attribute__((weak));
}
