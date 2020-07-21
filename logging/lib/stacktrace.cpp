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
#include <cstdio>
#include <map>
#include <string>

#include <mutex>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
extern "C" {
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <dirent.h>
#include <cxxabi.h>
#include <execinfo.h>
}

#include "logging.h"
#include "backtrace.h"

namespace sds_logging {
static bool g_custom_signal_handler_installed = false;
static std::atomic< int > g_stack_dump_outstanding = 0;
static std::condition_variable g_stack_dump_cv;

using SignalType = int;
using signame_handler_pair_t = std::pair< std::string, sig_handler_t >;

static void restore_signal_handler(int signal_number) {
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    struct sigaction action;
    memset(&action, 0, sizeof(action)); //
    sigemptyset(&action.sa_mask);
    action.sa_handler = SIG_DFL; // take default action for the signal
    sigaction(signal_number, &action, NULL);
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

static void exit_with_default_sighandler(SignalType fatal_signal_id) {
    const int signal_number = static_cast< int >(fatal_signal_id);
    restore_signal_handler(signal_number);

    if (signal_number != SIGINT) {
        std::cerr << "\n"
                  << __FUNCTION__ << ":" << __LINE__ << ". Exiting due to signal "
                  << ", " << signal_number << "   \n\n"
                  << std::flush;
    }

    kill(getpid(), signal_number);
    exit(signal_number);
}

/** \return signal_name Ref: signum.hpp and \ref installSignalHandler
 *  or for Windows exception name */
static std::string exit_reason_name(SignalType fatal_id) {
    int signal_number = static_cast< int >(fatal_id);
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

static void crash_handler(int signal_number, [[maybe_unused]] siginfo_t* info, [[maybe_unused]] void* unused_context) {
    // Only one signal will be allowed past this point
    if (exit_in_progress()) {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::ostringstream fatal_stream;
    const auto fatal_reason = exit_reason_name(signal_number);
    fatal_stream << "\n***** Received fatal SIGNAL: " << fatal_reason;
    fatal_stream << "(" << signal_number << ")\tPID: " << getpid();

    GetLogger()->set_pattern("%v");
    log_stack_trace(true);
    LOGCRITICAL("{}", fatal_stream.str());

    spdlog::apply_all([&](std::shared_ptr< spdlog::logger > l) { l->flush(); });
    spdlog::shutdown();

    exit_with_default_sighandler(signal_number);
}

static void sigint_handler(int signal_number, [[maybe_unused]] siginfo_t* info, [[maybe_unused]] void* unused_context) {
    spdlog::apply_all([&](std::shared_ptr< spdlog::logger > l) { l->flush(); });
    spdlog::shutdown();

    exit_with_default_sighandler(signal_number);
}

static void bt_dumper([[maybe_unused]] int signal_number, [[maybe_unused]] siginfo_t* info,
                      [[maybe_unused]] void* unused_context) {
    logger_thread_ctx.m_stack_buff[0] = 0;
    stack_backtrace(logger_thread_ctx.m_stack_buff, max_stacktrace_size());
    g_stack_dump_outstanding--;
    g_stack_dump_cv.notify_all();
}

static void log_stack_trace_all_threads() {
    std::unique_lock lk(LoggerThreadContext::_logger_thread_mutex);
    g_stack_dump_outstanding = LoggerThreadContext::_logger_thread_set.size();
    for (auto ctx : LoggerThreadContext::_logger_thread_set) {
        pthread_kill(ctx->m_thread_id, SIGUSR3);
    }

    auto& _l = GetLogger();
    auto& _cl = GetCriticalLogger();
    if (!_l || !_cl) {
        return;
    }

    g_stack_dump_cv.wait(lk, [] { return (g_stack_dump_outstanding == 0); });

    // First dump this thread context
    uint32_t thr_count = 1;
    _l->critical("Thread ID: {}, Thread num: {}\n{}", logger_thread_ctx.m_thread_id, 0, logger_thread_ctx.m_stack_buff);
    _cl->critical("Thread ID: {}, Thread num: {}\n{}", logger_thread_ctx.m_thread_id, 0,
                  logger_thread_ctx.m_stack_buff);
    for (auto ctx : LoggerThreadContext::_logger_thread_set) {
        if (ctx == &logger_thread_ctx) {
            continue;
        }
        _l->critical("Thread ID: {}, Thread num: {}\n{}", ctx->m_thread_id, thr_count, ctx->m_stack_buff);
        _cl->critical("Thread ID: {}, Thread num: {}\n{}", ctx->m_thread_id, thr_count, ctx->m_stack_buff);
        thr_count++;
    }
    _l->flush();
}

/************************************************* Exported APIs **********************************/
static std::map< SignalType, signame_handler_pair_t > g_sighandler_map = {
    {SIGABRT, {"SIGABRT", &crash_handler}}, {SIGFPE, {"SIGFPE", &crash_handler}}, {SIGILL, {"SIGILL", &crash_handler}},
    {SIGSEGV, {"SIGSEGV", &crash_handler}}, {SIGINT, {"SIGINT", &crash_handler}}, {SIGUSR3, {"SIGUSR3", &bt_dumper}},
    {SIGINT, {"SIGINT", &sigint_handler}},
};
static std::mutex install_hdlr_mutex;

void install_signal_handler() {
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    // sigaction to use sa_sigaction file. ref: http://www.linuxprogrammingblog.com/code-examples/sigaction

    // do it verbose style - install all signal actions
    for (const auto& sig_pair : g_sighandler_map) {
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        sigemptyset(&action.sa_mask);
        action.sa_sigaction = sig_pair.second.second; // callback to crashHandler for fatal signals
        action.sa_flags = SA_SIGINFO;

        if (sigaction(sig_pair.first, &action, nullptr) < 0) {
            const std::string error = "sigaction - " + sig_pair.second.first;
            perror(error.c_str());
        }
    }
    g_custom_signal_handler_installed = true;
#endif
}

void add_signal_handler(int sig_num, std::string_view sig_name, sig_handler_t hdlr) {
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    std::scoped_lock< std::mutex > l(install_hdlr_mutex);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = hdlr; // callback to crashHandler for fatal signals
    action.sa_flags = SA_SIGINFO;

    if (sigaction(sig_num, &action, nullptr) < 0) {
        const std::string error = "sigaction - " + std::string(sig_name);
        perror(error.c_str());
    }

    g_custom_signal_handler_installed = true;
    g_sighandler_map.emplace(std::make_pair(sig_num, std::make_pair(sig_name, hdlr)));
#else
    (void)sig_num;
    (void)sig_name;
    (void)hdlr;
#endif
}

void log_custom_signal_handlers() {
    std::string m;
    std::scoped_lock< std::mutex > l(install_hdlr_mutex);
    for (auto& sp : g_sighandler_map) {
        m += fmt::format("{}={}, ", sp.second.first, (void*)sp.second.second);
    }

    LOGINFO("Custom Signal handlers: {}", m);
}

void log_stack_trace(bool all_threads) {
    if (is_crash_handler_installed() && all_threads) {
        log_stack_trace_all_threads();
    } else {
        char buff[64 * 1024];
        buff[0] = 0;
        size_t s = stack_backtrace(buff, sizeof(buff));
        (void)s;
        LOGCRITICAL("\n\n{}", buff);
    }
}

void send_thread_signal(pthread_t thr, int sig_num) { pthread_kill(thr, sig_num); }

void install_crash_handler() { install_signal_handler(); }

bool is_crash_handler_installed() { return g_custom_signal_handler_installed; }

} // namespace sds_logging
