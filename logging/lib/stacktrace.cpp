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
#include <atomic>
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

namespace sds_logging {
static bool g_custom_signal_handler_installed{false};
static bool g_crash_handle_all_threads{true};
static std::atomic< size_t > g_stack_dump_outstanding{0};
static std::condition_variable g_stack_dump_cv;
static std::mutex g_hdlr_mutex;

typedef int SignalType;
typedef std::pair< std::string, sig_handler_t > signame_handler_pair_t;

static void restore_signal_handler(const int signal_number) {
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    std::scoped_lock< std::mutex > lock{g_hdlr_mutex};
    struct sigaction action;
    std::memset(static_cast<void*>(&action), 0, sizeof(action)); //
    ::sigemptyset(&action.sa_mask);
    action.sa_handler = SIG_DFL; // take default action for the signal
    ::sigaction(signal_number, &action, NULL);

    g_custom_signal_handler_installed = false;
#endif
}

static bool exit_in_progress() {
    static std::atomic< pthread_t > tracing_thread_id{0};
    bool ret;
    pthread_t id;
    pthread_t new_id;

    do {
        id = tracing_thread_id.load();
        if ((id == 0) || (id == pthread_self())) {
            ret = false;
            new_id = pthread_self();
        } else {
            ret = true;
            break;
        }
    } while (!tracing_thread_id.compare_exchange_weak(id, new_id));

    return ret;
}

static void exit_with_default_sighandler(const SignalType fatal_signal_id) {
    const int signal_number{static_cast< int >(fatal_signal_id)};
    restore_signal_handler(signal_number);

    if (signal_number != SIGINT) {
        std::cerr << "\n"
                  << __FUNCTION__ << ":" << __LINE__ << ". Exiting due to signal "
                  << ", " << signal_number << "   \n\n"
                  << std::flush;
    }

    ::kill(::getpid(), signal_number);
    std::exit(signal_number);
}

/** \return signal_name Ref: signum.hpp and \ref installSignalHandler
 *  or for Windows exception name */
static std::string exit_reason_name(const SignalType fatal_id) {
    const int signal_number{static_cast< int >(fatal_id)};
    switch (signal_number) {
    case SIGABRT: return "SIGABRT"; break;
    case SIGFPE: return "SIGFPE"; break;
    case SIGSEGV: return "SIGSEGV"; break;
    case SIGILL: return "SIGILL"; break;
    case SIGTERM: return "SIGTERM"; break;
    case SIGINT: return "SIGINT"; break;
    default:
        std::ostringstream oss;
        oss << "UNKNOWN SIGNAL(" << signal_number << ")"; // for " << level.text;
        return oss.str();
    }
}

static void crash_handler(const int signal_number, [[maybe_unused]] siginfo_t* const info, [[maybe_unused]] void* const unused_context) {
    // Only one signal will be allowed past this point
    if (exit_in_progress()) {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds{1});
        }
    }

    std::ostringstream fatal_stream;
    const auto fatal_reason{exit_reason_name(signal_number)};
    fatal_stream << "\n***** Received fatal SIGNAL: " << fatal_reason;
    fatal_stream << "(" << signal_number << ")\tPID: " << ::getpid();

    GetLogger()->set_pattern("%v");
    log_stack_trace(g_crash_handle_all_threads);
    LOGCRITICAL("{}", fatal_stream.str());

    spdlog::apply_all([&](std::shared_ptr< spdlog::logger > l) { l->flush(); });
    spdlog::shutdown();

    exit_with_default_sighandler(signal_number);
}

static void sigint_handler(const int signal_number, [[maybe_unused]] siginfo_t* const info, [[maybe_unused]] void* const unused_context) {
    spdlog::apply_all([&](std::shared_ptr< spdlog::logger > l) { l->flush(); });
    spdlog::shutdown();

    exit_with_default_sighandler(signal_number);
}

static void bt_dumper([[maybe_unused]] const int signal_number, [[maybe_unused]] siginfo_t* const info,
                      [[maybe_unused]] void* const unused_context) {
    logger_thread_ctx.m_stack_buff[0] = 0;
    stack_backtrace(logger_thread_ctx.m_stack_buff.data(), logger_thread_ctx.m_stack_buff.size());
    --g_stack_dump_outstanding;
    g_stack_dump_cv.notify_all();
}

static void log_stack_trace_all_threads() {
    std::unique_lock lk{LoggerThreadContext::_logger_thread_mutex};
    auto& logger{GetLogger()};
    auto& critical_logger{GetLogger()};
    size_t thread_count{1};

    const auto dump_thread{[&lk, &logger, &critical_logger, &thread_count](const bool signal_thread, auto* ctx) {
        if (signal_thread) {
            g_stack_dump_outstanding = 1;
            send_thread_signal(ctx->m_thread_id, SIGUSR3);

            g_stack_dump_cv.wait(lk, [] { return (g_stack_dump_outstanding.load() == 0); });
        } else {
            // dump the thread without recursive signal
            ctx->m_stack_buff[0] = 0;
            stack_backtrace(ctx->m_stack_buff.data(), ctx->m_stack_buff.size());
        }

        if (logger) {
            logger->critical("Thread ID: {}, Thread num: {}\n{}", ctx->m_thread_id, thread_count,
                             ctx->m_stack_buff.data());
            logger->flush();
        }
        else if (critical_logger) {
            critical_logger->critical("Thread ID: {}, Thread num: {}\n{}", ctx->m_thread_id, thread_count,
                                      ctx->m_stack_buff.data());
            critical_logger->flush();
        }
    }};

    // flush logs
    if (logger)
        logger->flush();
    if (critical_logger)
        critical_logger->flush();

    // First dump this thread context
    dump_thread(false, &logger_thread_ctx);
    ++thread_count;

    // dump other threads
    for (auto* const ctx : LoggerThreadContext::_logger_thread_set) {
        if (ctx == &logger_thread_ctx) {
            continue;
        }
        dump_thread(true, ctx);
        ++thread_count;
    }
}

/************************************************* Exported APIs **********************************/
static std::map< SignalType, signame_handler_pair_t > g_sighandler_map = {
    {SIGABRT, {"SIGABRT", &crash_handler}}, {SIGFPE, {"SIGFPE", &crash_handler}}, {SIGILL, {"SIGILL", &crash_handler}},
    {SIGSEGV, {"SIGSEGV", &crash_handler}}, {SIGINT, {"SIGINT", &crash_handler}}, {SIGUSR3, {"SIGUSR3", &bt_dumper}},
    {SIGINT, {"SIGINT", &sigint_handler}},
};

void install_signal_handler(const bool all_threads) {
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    // sigaction to use sa_sigaction file. ref: http://www.linuxprogrammingblog.com/code-examples/sigaction
    std::scoped_lock< std::mutex > l{g_hdlr_mutex};

    // do it verbose style - install all signal actions
    for (const auto& sig_pair : g_sighandler_map) {
        struct sigaction action;
        std::memset(static_cast<void*>(&action), 0, sizeof(action));
        ::sigemptyset(&action.sa_mask);
        action.sa_sigaction = sig_pair.second.second; // callback to crashHandler for fatal signals
        action.sa_flags = SA_SIGINFO;

        if (::sigaction(sig_pair.first, &action, nullptr) < 0) {
            const std::string error{std::string{"sigaction - "} + sig_pair.second.first};
            ::perror(error.c_str());
        }
    }
    g_crash_handle_all_threads = all_threads;
    g_custom_signal_handler_installed = true;
#endif
}

void add_signal_handler([[maybe_unused]] const int sig_num, [[maybe_unused]] const std::string_view& sig_name,
                        [[maybe_unused]] sig_handler_t hdlr) {
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    std::scoped_lock< std::mutex > l{g_hdlr_mutex};
    struct sigaction action;
    std::memset(static_cast<void*>(&action), 0, sizeof(action));
    ::sigemptyset(&action.sa_mask);
    action.sa_sigaction = hdlr; // callback to crashHandler for fatal signals
    action.sa_flags = SA_SIGINFO;

    if (::sigaction(sig_num, &action, nullptr) < 0) {
        const std::string error{std::string{"sigaction - "} + std::string(sig_name)};
        ::perror(error.c_str());
    }

    g_custom_signal_handler_installed = true;
    g_sighandler_map.emplace(std::make_pair(sig_num, std::make_pair(sig_name, hdlr)));
#endif
}

void log_custom_signal_handlers() {
    std::string m;
    {
        std::scoped_lock< std::mutex > l{g_hdlr_mutex};
        for (const auto& sp : g_sighandler_map) {
            m += fmt::format("{}={}, ", sp.second.first, reinterpret_cast<const void*>(sp.second.second));
        }
    }

    LOGINFO("Custom Signal handlers: {}", m);
}

void log_stack_trace(const bool all_threads) {
    if (is_crash_handler_installed() && all_threads) {
        log_stack_trace_all_threads();
    } else {
        // make this static so that no memory allocation is necessary
        static std::array < char, max_stacktrace_size()> buff;
        buff[0] = 0;
        [[maybe_unused]] const size_t s{stack_backtrace(buff.data(), buff.size())};
        LOGCRITICAL("\n\n{}", buff.data());
    }
}

void send_thread_signal(const pthread_t thr, const int sig_num) { ::pthread_kill(thr, sig_num); }

void install_crash_handler(const bool all_threads) { install_signal_handler(all_threads); }

bool is_crash_handler_installed() { return g_custom_signal_handler_installed; }

} // namespace sds_logging
