/*
 * stacktrace.cpp
 *
 * Copyright (c) 2018 by eBay Corporation
 *
 * Some portion of this module is taken from g3log and spdlog fork by rxdu (especially *_signal_handler methods)
 *
 * On top of that added functionalities to dump stack trace, signal for every thread and then dump it, etc.
 *
 */

#include <array>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#if defined(__linux__) || defined(__APPLE__)
#include <dirent.h>
#include <execinfo.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "backtrace.h"
#include "logging.h"

namespace {
constexpr uint64_t backtrace_timeout_ms{4 * backtrace_detail::pipe_timeout_ms};
}

namespace sisl {
namespace logging {
static bool g_custom_signal_handler_installed{false};
static size_t g_custom_signal_handlers{0};
static bool g_crash_handle_all_threads{true};
static std::mutex g_mtx_stack_dump_outstanding;
static size_t g_stack_dump_outstanding{0};
static std::condition_variable g_stack_dump_cv;
static std::mutex g_hdlr_mutex;
static std::array< char, max_stacktrace_size() > g_stacktrace_buff;

typedef struct SignalHandlerData {
    SignalHandlerData(std::string name, const sig_handler_t handler) : name{std::move(name)}, handler{handler} {}
    std::string name;
    sig_handler_t handler;
    size_t num_installed{0};
} signame_handler_data_t;

static bool exit_in_progress() {
    static std::atomic< pthread_t > tracing_thread_id{0};
    auto& logger{GetLogger()};
    auto& critical_logger{GetCriticalLogger()};
    pthread_t current_id{tracing_thread_id.load()};
    pthread_t new_id{pthread_self()};

    if (logger) { logger->critical("Thread num: {} entered exit handler\n", new_id); }
    if (critical_logger) { critical_logger->critical("Thread num: {} entered exit handler\n", new_id); }

    if (current_id == new_id) {
        // we are already marked in exit handler
        return false;
    } else if (current_id != 0) {
        // another thread already marked in exit handler
        return true;
    }
    // try to mark this thread as the active exit handler
    return !tracing_thread_id.compare_exchange_strong(current_id, new_id);
}

static void exit_with_default_sighandler(const SignalType fatal_signal_id) {
    restore_signal_handlers(); // restore all custom handlers to default

    if (fatal_signal_id != SIGINT) {
        std::cerr << "\n"
                  << __FUNCTION__ << ":" << __LINE__ << ". Exiting due to signal "
                  << ", " << fatal_signal_id << "   \n\n"
                  << std::flush;
    }

    //::kill(::getpid(), fatal_signal_id);
    if (fatal_signal_id == SIGABRT) {
        std::_Exit(fatal_signal_id);
    } else {
        std::exit(fatal_signal_id);
    }
}

/** \return signal_name Ref: signum.hpp and \ref installSignalHandler
 *  or for Windows exception name */
static const char* exit_reason_name(const SignalType fatal_id) {
    switch (fatal_id) {
    case SIGABRT:
        return "SIGABRT";
        break;
    case SIGFPE:
        return "SIGFPE";
        break;
    case SIGSEGV:
        return "SIGSEGV";
        break;
    case SIGILL:
        return "SIGILL";
        break;
    case SIGTERM:
        return "SIGTERM";
        break;
    case SIGINT:
        return "SIGINT";
        break;
    default:
        static std::array< char, 30 > unknown;
        std::snprintf(unknown.data(), unknown.size(), "UNKNOWN SIGNAL(%i)", fatal_id);
        return unknown.data();
    }
}

static void crash_handler(const SignalType signal_number) {
    const auto flush_logs{[]() { // flush all logs
        spdlog::apply_all([&](std::shared_ptr< spdlog::logger > l) {
            if (l) l->flush();
        });
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
    }};

    // Only one signal will be allowed past this point
    if (exit_in_progress()) {
        // already have thread in exit handler so just spin
        flush_logs();
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds{1});
        }
    } else {
        flush_logs();
    }

    // remove all default logging info except for message
    GetLogger()->set_pattern("%v");
    log_stack_trace(g_crash_handle_all_threads);
    LOGCRITICAL("\n * ****Received fatal SIGNAL : {}({})\tPID : {}", exit_reason_name(signal_number), signal_number,
                ::getpid());

    // flush again and shutdown
    flush_logs();
    spdlog::shutdown();

    exit_with_default_sighandler(signal_number);
}

static void sigint_handler(const SignalType signal_number) {
    // flush all logs and shutdown
    spdlog::apply_all([&](std::shared_ptr< spdlog::logger > l) {
        if (l) l->flush();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    spdlog::shutdown();

    exit_with_default_sighandler(signal_number);
}

static void bt_dumper([[maybe_unused]] const SignalType signal_number) {
    g_stacktrace_buff.fill(0);
    stack_backtrace(g_stacktrace_buff.data(), g_stacktrace_buff.size(), true);
    bool notify{false};
    {
        std::unique_lock lock{g_mtx_stack_dump_outstanding};
        if (g_stack_dump_outstanding > 0) {
            --g_stack_dump_outstanding;
            notify = true;
        }
    }
    if (notify) g_stack_dump_cv.notify_all();
}

static void log_stack_trace_all_threads() {
    std::unique_lock logger_lock{LoggerThreadContext::s_logger_thread_mutex};
    auto& logger{GetLogger()};
    auto& critical_logger{GetCriticalLogger()};
    size_t thread_count{1};

    const auto dump_thread{[&logger, &critical_logger, &thread_count](const bool signal_thread, const auto thread_id) {
        if (signal_thread) {
            const auto log_failure{[&logger, &critical_logger, &thread_count, &thread_id](const char* const msg) {
                if (logger) {
                    logger->critical("Thread ID: {}, Thread num: {} - {}\n", thread_id, thread_count, msg);
                    logger->flush();
                }
                if (critical_logger) {
                    critical_logger->critical("Thread ID: {}, Thread num: {} - {}\n", thread_id, thread_count, msg);
                    critical_logger->flush();
                }
            }};

            {
                std::unique_lock outstanding_lock{g_mtx_stack_dump_outstanding};
                assert(g_stack_dump_outstanding == 0);
                g_stack_dump_outstanding = 1;
            }
            if (!send_thread_signal(thread_id, SIGUSR3)) {
                {
                    std::unique_lock outstanding_lock{g_mtx_stack_dump_outstanding};
                    g_stack_dump_outstanding = 0;
                }
                log_failure("Invalid/terminated thread");
                return;
            }

            {
                std::unique_lock outstanding_lock{g_mtx_stack_dump_outstanding};
                const auto result{g_stack_dump_cv.wait_for(outstanding_lock,
                                                           std::chrono::milliseconds{backtrace_timeout_ms},
                                                           [] { return (g_stack_dump_outstanding == 0); })};
                if (!result) {
                    g_stack_dump_outstanding = 0;
                    outstanding_lock.unlock();
                    log_failure("Timeout waiting for stacktrace");
                    return;
                }
            }
        } else {
            // dump the thread without recursive signal
            g_stacktrace_buff.fill(0);
            stack_backtrace(g_stacktrace_buff.data(), g_stacktrace_buff.size(), true);
        }

        if (logger) {
            logger->critical("Thread ID: {}, Thread num: {}\n{}", thread_id, thread_count, g_stacktrace_buff.data());
            logger->flush();
        }
        if (critical_logger) {
            critical_logger->critical("Thread ID: {}, Thread num: {}\n{}", thread_id, thread_count,
                                      g_stacktrace_buff.data());
            critical_logger->flush();
        }
    }};

    // First dump this thread context
    dump_thread(false, logger_thread_ctx.m_thread_id);
    ++thread_count;

    // dump other threads
    for (auto* const ctx : LoggerThreadContext::s_logger_thread_set) {
        if (ctx == &logger_thread_ctx) { continue; }
        dump_thread(true, ctx->m_thread_id);
        ++thread_count;
    }
}

/************************************************* Exported APIs **********************************/
static std::map< SignalType, signame_handler_data_t > g_sighandler_map{
    {SIGABRT, {"SIGABRT", &crash_handler}},
    {SIGFPE, {"SIGFPE", &crash_handler}},
    {SIGILL, {"SIGILL", &crash_handler}},
    {SIGSEGV, {"SIGSEGV", &crash_handler}},
    /* {SIGINT, {"SIGINT", &crash_handler}}, */
    {SIGUSR3, {"SIGUSR3", &bt_dumper}},
    {SIGINT, {"SIGINT", &sigint_handler}},
};

// restore given signal_numbder signal handler to default
bool restore_signal_handler(const SignalType signal_number) {
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    std::lock_guard< std::mutex > lock{g_hdlr_mutex};
    auto* const old_signal{std::signal(signal_number, SIG_DFL)};
    if (old_signal == SIG_ERR) {
        static std::array< char, 30 > error;
        std::snprintf(error.data(), error.size(), "default sigaction - %i", signal_number);
        std::perror(error.data());
        return false;
    } else {
        // restoring custom handler so decrease number of custom handlers
        auto itr{g_sighandler_map.find(signal_number)};
        if (itr != std::end(g_sighandler_map)) {
            g_custom_signal_handlers -= itr->second.num_installed;
            itr->second.num_installed = 0;
            if (g_custom_signal_handlers == 0) { g_custom_signal_handler_installed = false; }
        }
        return true;
    }
#else
    return true;
#endif
}

// restore all custom signal handlers to default
bool restore_signal_handlers() {
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    std::lock_guard< std::mutex > lock{g_hdlr_mutex};

    bool failed{false};
    for (auto& sig_pair : g_sighandler_map) {
        auto* const old_signal{std::signal(sig_pair.first, SIG_DFL)};
        if (old_signal == SIG_ERR) {
            static std::array< char, 30 > error;
            std::snprintf(error.data(), error.size(), "default sigaction - %i", sig_pair.first);
            std::perror(error.data());
            failed = true;
        } else {
            // restoring custom handler so decrease number of custom handlers
            auto itr{g_sighandler_map.find(sig_pair.first)};
            if (itr != std::end(g_sighandler_map)) {
                g_custom_signal_handlers -= itr->second.num_installed;
                itr->second.num_installed = 0;
                if (g_custom_signal_handlers == 0) { g_custom_signal_handler_installed = false; }
            }
        }
    }
    return !failed;
#else
    return true;
#endif
}

bool install_signal_handler(const bool all_threads) {
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    static std::array< char, 30 > error;
    std::lock_guard< std::mutex > lock{g_hdlr_mutex};

    // do it verbose style - install all signal actions
    size_t num_installed{0};
    bool failed{false};
    for (auto& sig_pair : g_sighandler_map) {
        auto* const old_signal{std::signal(sig_pair.first, sig_pair.second.handler)};
        if (old_signal == SIG_ERR) {
            std::snprintf(error.data(), error.size(), "sigaction - %s", sig_pair.second.name.c_str());
            std::perror(error.data());
            failed = true;
            break;
        } else if (old_signal != sig_pair.second.handler) {
            // installed new custom signal handler
            ++g_custom_signal_handlers;
            ++(sig_pair.second.num_installed);
        }
        ++num_installed;
    }

    if (!failed) {
        if (g_custom_signal_handlers > 0) {
            g_crash_handle_all_threads = all_threads;
            g_custom_signal_handler_installed = true;
        }
    } else {
        // roll back to default handlers
        if (num_installed > 0) {
            size_t rolled_back{0};
            for (auto& sig_pair : g_sighandler_map) {
                auto* const old_signal{std::signal(sig_pair.first, SIG_DFL)};
                if (old_signal == SIG_ERR) {
                    std::snprintf(error.data(), error.size(), "default sigaction - %s", sig_pair.second.name.c_str());
                    std::perror(error.data());
                } else if (old_signal == sig_pair.second.handler) {
                    // restoring custom handler so decrease number of custom
                    g_custom_signal_handlers -= sig_pair.second.num_installed;
                    sig_pair.second.num_installed = 0;
                    if (g_custom_signal_handlers == 0) { g_custom_signal_handler_installed = false; }
                }
                if (++rolled_back == num_installed) break;
            }
        }
    }
    return !failed;
#else
    return false;
#endif
}

bool add_signal_handler([[maybe_unused]] const SignalType sig_num, [[maybe_unused]] const std::string_view& sig_name,
                        [[maybe_unused]] const sig_handler_t hdlr) {
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    std::lock_guard< std::mutex > lock{g_hdlr_mutex};
    auto* const old_signal{std::signal(sig_num, hdlr)};
    if (old_signal == SIG_ERR) {
        static std::array< char, 30 > error;
        std::snprintf(error.data(), error.size(), "sigaction - %.*s", static_cast< int >(sig_name.size()),
                      sig_name.data());
        std::perror(error.data());
        return false;
    } else if (old_signal != hdlr) {
        // installed new custom handler for signal
        ++g_custom_signal_handlers;
        g_custom_signal_handler_installed = true;

        // add/update mapping of signal to handler
        const auto itr_pair{g_sighandler_map.emplace(sig_num, signame_handler_data_t{sig_name.data(), hdlr})};
        itr_pair.first->second.handler = hdlr;
        ++(itr_pair.first->second.num_installed);
    }
    return true;
#else
    return false;
#endif
}

void log_custom_signal_handlers() {
    std::string m;
    {
        std::lock_guard< std::mutex > lock{g_hdlr_mutex};
        for (const auto& sp : g_sighandler_map) {
            m += fmt::format("{}={}, ", sp.second.name, reinterpret_cast< const void* >(sp.second.handler));
        }
    }

    LOGINFO("Custom Signal handlers: {}", m);
}

void log_stack_trace(const bool all_threads) {
    if (is_crash_handler_installed() && all_threads) {
        log_stack_trace_all_threads();
    } else {
        // make this static so that no memory allocation is necessary
        static std::array< char, max_stacktrace_size() > buff;
        buff.fill(0);
        [[maybe_unused]] const size_t s{stack_backtrace(buff.data(), buff.size(), true)};
        LOGCRITICAL("\n\n{}", buff.data());
    }
}

bool send_thread_signal(const pthread_t thr, const SignalType sig_num) { return (::pthread_kill(thr, sig_num) == 0); }

bool install_crash_handler(const bool all_threads) { return install_signal_handler(all_threads); }

bool is_crash_handler_installed() {
    std::lock_guard< std::mutex > lock{g_hdlr_mutex};
    return g_custom_signal_handler_installed;
}
} // namespace logging
} // namespace sisl
