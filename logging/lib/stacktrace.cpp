/*
 * stacktrace.cpp
 *
 * Copyright (c) 2018 by eBay Corporation
 * 
 * Small portion of this module is taken from g3log and spdlog fork by rxdu (especially *_signal_handler methods)
 *
 * On top of that added functionalities to dump stack trace, signal for every thread and then dump it, etc.
 *
 */
#include <cstdio>
#include <map>
#include <string>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
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
#if defined(__linux__)
#define SIGUSR3     SIGRTMIN + 1
#else
#define SIGUSR3     SIGUSR1
#endif

const static std::map<int, std::string> kSignals = {
    {SIGABRT, "SIGABRT"}, {SIGFPE, "SIGFPE"}, {SIGILL, "SIGILL"}, {SIGSEGV, "SIGSEGV"}, {SIGINT, "SIGINT"}, {SIGUSR3, "SIGUSR3"}};

static std::map<int, std::string> gSignals = kSignals;
static bool g_signal_handler_installed = false;
static std::atomic< int > g_stack_dump_outstanding = 0;
static std::condition_variable g_stack_dump_cv;

typedef int SignalType;

static void restore_signal_handler(int signal_number) {
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    struct sigaction action;
    memset(&action, 0, sizeof(action)); //
    sigemptyset(&action.sa_mask);
    action.sa_handler = SIG_DFL; // take default action for the signal
    sigaction(signal_number, &action, NULL);
#endif
}

#if 0
/** return whether or any fatal handling is still ongoing
 *  not used
 *  only in the case of Windows exceptions (not fatal signals)
 *  are we interested in changing this from false to true to
 *  help any other exceptions handler work with 'EXCEPTION_CONTINUE_SEARCH'*/
static bool shouldBlockForFatalHandling() {
    return true; // For windows we will after fatal processing change it to false
}
#endif

static bool should_do_exit() {
    static std::atomic<uint64_t> firstExit{0};
    auto const count = firstExit.fetch_add(1, std::memory_order_relaxed);
    return (0 == count);
}

static void exit_with_default_sighandler(SignalType fatal_signal_id) {
    const int signal_number = static_cast<int>(fatal_signal_id);
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
    int signal_number = static_cast<int>(fatal_id);
    switch (signal_number) {
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
        std::ostringstream oss;
        oss << "UNKNOWN SIGNAL(" << signal_number << ")"; // for " << level.text;
        return oss.str();
    }
}

static void signal_handler(int signal_number, siginfo_t *info, void *unused_context) {
    // Make compiler happy about unused variables
    (void)info;
    (void)unused_context;

    if (signal_number == SIGUSR3) {
        logger_thread_ctx.m_stack_buff[0] = 0;
        stack_backtrace(logger_thread_ctx.m_stack_buff, max_stacktrace_size());
        g_stack_dump_outstanding--;
        g_stack_dump_cv.notify_all();
        return;
    }

    // Only one signal will be allowed past this point
    if (false == should_do_exit()) {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // No stack dump and message if signal is SIGINT, which is usually raised by user
    if (signal_number != SIGINT) {
        std::ostringstream fatal_stream;
        const auto fatal_reason = exit_reason_name(signal_number);
        fatal_stream << "\n***** Received fatal SIGNAL: " << fatal_reason;
        fatal_stream << "(" << signal_number << ")\tPID: " << getpid();

        mythread_logger->set_pattern("%v");
        log_stack_trace(true);
        LOGCRITICAL("{}", fatal_stream.str());
    }

    spdlog::apply_all([&](std::shared_ptr<spdlog::logger> l) {l->flush();});
    spdlog::shutdown();

    exit_with_default_sighandler(signal_number);
}

void install_signal_handler() {
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = &signal_handler; // callback to crashHandler for fatal signals
    // sigaction to use sa_sigaction file. ref: http://www.linuxprogrammingblog.com/code-examples/sigaction
    action.sa_flags = SA_SIGINFO;

    // do it verbose style - install all signal actions
    for (const auto &sig_pair : gSignals) { 
        if (sigaction(sig_pair.first, &action, nullptr) < 0) { 
	    const std::string error = "sigaction - " + sig_pair.second;
            perror(error.c_str());
        }
    }
    g_signal_handler_installed = true;
#endif
}

void install_crash_handler() {
    install_signal_handler();
}

bool is_crash_handler_installed() { return g_signal_handler_installed; }

void install_crash_handler_once() {
    static std::once_flag signal_install_flag;
    std::call_once(signal_install_flag, [] { install_signal_handler(); });
}

/// Overrides the existing signal handling for custom signals
/// For example: usage of zcmq relies on its own signal handler for SIGTERM
///     so users with zcmq should then use the @ref overrideSetupSignals
///     , likely with the original set of signals but with SIGTERM removed
///
/// call example:
///  g3::overrideSetupSignals({ {SIGABRT, "SIGABRT"}, {SIGFPE, "SIGFPE"},{SIGILL, "SIGILL"},
//                          {SIGSEGV, "SIGSEGV"},});
void override_setup_signals(const std::map<int, std::string> override_signals) {
    static std::mutex signal_lock;
    std::lock_guard<std::mutex> guard(signal_lock);
    for (const auto &sig : gSignals) {
        restore_signal_handler(sig.first);
    }

    gSignals = override_signals;
    install_crash_handler(); // installs all the signal handling for gSignals
}

/// Probably only needed for unit testing. Resets the signal handling back to default
/// which might be needed in case it was previously overridden
/// The default signals are: SIGABRT, SIGFPE, SIGILL, SIGSEGV, SIGTERM
void restore_signal_handler_to_default() {
    override_setup_signals(kSignals);
}

static void log_stack_trace_all_threads() {
    std::unique_lock lk(LoggerThreadContext::_logger_thread_mutex);
    g_stack_dump_outstanding = LoggerThreadContext::_logger_thread_set.size();
    for (auto ctx : LoggerThreadContext::_logger_thread_set) {
        pthread_kill(ctx->m_thread_id, SIGUSR3);
    }

    auto& _l = GetLogger();
    if (!_l) { return; }

    g_stack_dump_cv.wait(lk, []{ return (g_stack_dump_outstanding == 0); });

    // First dump this thread context
    uint32_t thr_count = 1;
    _l->critical("Thread ID: {}, Thread num: {}\n{}", logger_thread_ctx.m_thread_id, 0, logger_thread_ctx.m_stack_buff);
    for (auto ctx : LoggerThreadContext::_logger_thread_set) {
        if (ctx == &logger_thread_ctx) { continue; }
        _l->critical("Thread ID: {}, Thread num: {}\n{}", ctx->m_thread_id, thr_count, ctx->m_stack_buff);
        thr_count++;
    }
    _l->flush();
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
}
