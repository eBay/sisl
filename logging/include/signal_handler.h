/** ==========================================================================
 * 2011 by KjellKod.cc. This is PUBLIC DOMAIN to use at your own risk and comes
 * with no warranties. This code is yours to share, use and modify with no
 * strings attached and no restrictions or obligations.
 *
 * For more information see g3log/LICENSE or refer refer to http://unlicense.org
 * ============================================================================*/
/*
 * This module was originally developed by KjellKod for g3log(https://github.com/KjellKod/g3log).
 *
 * Ported to spdlog by rxdu, inspired by the following discussions:
 * [1] https://github.com/gabime/spdlog/issues/293
 * [2] https://github.com/gabime/spdlog/issues/269
 *
 * Changes made:
 * 1. Removed all g3log related header includes and function calls
 * 2. Public API functions are put inside spdlog namespace, others are in spdlog::internal
 * 3. Added handling of SIGINT: flush logger and exit without stackdump
 * 4. Made functions inline and global variables static
 * 5. Updated comments to reflect changes
 *
 * Default workflow:
 * 1. Install signal handlers by calling installCrashHandler()/installCrashHandlerOnce()  
 * 2. (Normal logging operations)
 * 3. Capture signal and invoke signalHandler()
 *      - Dump stack and generate error message
 *      - Log stack information and error message
 *      - Exit by calling exitWithDefaultSignalHandler()
 *
 * Known limitations:
 * 1. Only functions in "crashhandler_unix.cpp" are ported, thus no Windows support.
 * 2. Not extensively tested for all use cases of spdlog.
 * 3. Not extensively tested on all supported platforms.
 */

#pragma once

#include <cstdio>
#include <map>
#include <string>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <execinfo.h>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <unistd.h>

#include "spdlog/spdlog.h"

#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32__) && !defined(__GNUC__))
#error "Disabled fatal signal handling due to imcompatible OS or compiler"
#define DISABLE_FATAL_SIGNALHANDLING 1
#endif

// Linux/Clang, OSX/Clang, OSX/gcc
#if (defined(__clang__) || defined(__APPLE__))
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

namespace sds_logging {

typedef int SignalType;

namespace internal {
inline void restoreSignalHandler(int signal_number)
{
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    struct sigaction action;
    memset(&action, 0, sizeof(action)); //
    sigemptyset(&action.sa_mask);
    action.sa_handler = SIG_DFL; // take default action for the signal
    sigaction(signal_number, &action, NULL);
#endif
}

/** return whether or any fatal handling is still ongoing
 *  not used
 *  only in the case of Windows exceptions (not fatal signals)
 *  are we interested in changing this from false to true to
 *  help any other exceptions handler work with 'EXCEPTION_CONTINUE_SEARCH'*/
inline bool shouldBlockForFatalHandling()
{
    return true; // For windows we will after fatal processing change it to false
}

/** \return signal_name Ref: signum.hpp and \ref installSignalHandler
 *  or for Windows exception name */
inline std::string exitReasonName(SignalType fatal_id)
{
    int signal_number = static_cast<int>(fatal_id);
    switch (signal_number)
    {
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

/** return calling thread's stackdump*/
inline std::string stackdump(const char *rawdump = nullptr)
{
    if (nullptr != rawdump && !std::string(rawdump).empty())
    {
        return {rawdump};
    }

    const size_t max_dump_size = 50;
    void *dump[max_dump_size];
    size_t size = backtrace(dump, max_dump_size);
    char **messages = backtrace_symbols(dump, static_cast<int>(size)); // overwrite sigaction with caller's address

    // dump stack: skip first frame, since that is here
    std::ostringstream oss;
    for (size_t idx = 1; idx < size && messages != nullptr; ++idx)
    {
        char *mangled_name = 0, *offset_begin = 0, *offset_end = 0;
        // find parantheses and +address offset surrounding mangled name
        for (char *p = messages[idx]; *p; ++p)
        {
            if (*p == '(')
            {
                mangled_name = p;
            }
            else if (*p == '+')
            {
                offset_begin = p;
            }
            else if (*p == ')')
            {
                offset_end = p;
                break;
            }
        }

        // if the line could be processed, attempt to demangle the symbol
        if (mangled_name && offset_begin && offset_end && mangled_name < offset_begin)
        {
            *mangled_name++ = '\0';
            *offset_begin++ = '\0';
            *offset_end++ = '\0';

            int status;
            char *real_name = abi::__cxa_demangle(mangled_name, 0, 0, &status);
            // if demangling is successful, output the demangled function name
            if (status == 0)
            {
                oss << "\n\tstack dump [" << idx << "]  " << messages[idx] << " : " << real_name << "+";
                oss << offset_begin << offset_end << std::endl;
            } // otherwise, output the mangled function name
            else
            {
                oss << "\tstack dump [" << idx << "]  " << messages[idx] << mangled_name << "+";
                oss << offset_begin << offset_end << std::endl;
            }
            free(real_name); // mallocated by abi::__cxa_demangle(...)
        }
        else
        {
            // no demangling done -- just dump the whole line
            oss << "\tstack dump [" << idx << "]  " << messages[idx] << "  mangled_name unavailable" << std::endl;
        }
    } // END: for(size_t idx = 1; idx < size && messages != nullptr; ++idx)
    free(messages);
    return oss.str();
}

/** Re-"throw" a fatal signal, previously caught. This will exit the application
 * This is an internal only function. Do not use it elsewhere. It is triggered
 * from signalHandler() after flushing messages to file */
inline void exitWithDefaultSignalHandler(SignalType fatal_signal_id)
{
    const int signal_number = static_cast<int>(fatal_signal_id);
    restoreSignalHandler(signal_number);

    if (signal_number != SIGINT)
        std::cerr << "\n\n"
                  << __FUNCTION__ << ":" << __LINE__ << ". Exiting due to signal "
                  << ", " << signal_number << "   \n\n"
                  << std::flush;

    kill(getpid(), signal_number);
    exit(signal_number);
}

const static std::map<int, std::string> kSignals = {
    {SIGABRT, "SIGABRT"}, {SIGFPE, "SIGFPE"}, {SIGILL, "SIGILL"}, {SIGSEGV, "SIGSEGV"}, {SIGTERM, "SIGTERM"}, {SIGINT, "SIGINT"}};

static std::map<int, std::string> gSignals = kSignals;

inline bool shouldDoExit()
{
    static std::atomic<uint64_t> firstExit{0};
    auto const count = firstExit.fetch_add(1, std::memory_order_relaxed);
    return (0 == count);
}

// Dump of stack, then exit.
// ALL thanks to this thread at StackOverflow. Pretty much borrowed from:
// Ref: http://stackoverflow.com/questions/77005/how-to-generate-a-stacktrace-when-my-gcc-c-app-crashes
inline void signalHandler(int signal_number, siginfo_t *info, void *unused_context)
{
    // Make compiler happy about unused variables
    (void)info;
    (void)unused_context;

    // Only one signal will be allowed past this point
    if (false == shouldDoExit())
    {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // No stack dump and message if signal is SIGINT, which is usually raised by user
    if (signal_number != SIGINT)
    {
        const auto dump = stackdump();
        std::ostringstream fatal_stream;
        const auto fatal_reason = exitReasonName(signal_number);
        fatal_stream << "Received fatal signal: " << fatal_reason;
        fatal_stream << "(" << signal_number << ")\tPID: " << getpid() << std::endl;
        fatal_stream << "\n***** SIGNAL " << fatal_reason << "(" << signal_number << ")" << std::endl;

        std::string dumpstr(dump.c_str());
        spdlog::apply_all([&](std::shared_ptr<spdlog::logger> l) {
            l->critical(dumpstr);
            l->critical(fatal_stream.str());
        });
    }

    spdlog::apply_all([&](std::shared_ptr<spdlog::logger> l) {l->flush();});
    spdlog::shutdown();

    exitWithDefaultSignalHandler(signal_number);
}

//
// Installs FATAL signal handler that is enough to handle most fatal events
//  on *NIX systems
inline void installSignalHandler()
{
#if !(defined(DISABLE_FATAL_SIGNALHANDLING))
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = &signalHandler; // callback to crashHandler for fatal signals
    // sigaction to use sa_sigaction file. ref: http://www.linuxprogrammingblog.com/code-examples/sigaction
    action.sa_flags = SA_SIGINFO;

    // do it verbose style - install all signal actions
    for (const auto &sig_pair : gSignals)
    {
        if (sigaction(sig_pair.first, &action, nullptr) < 0)
        {
            const std::string error = "sigaction - " + sig_pair.second;
            perror(error.c_str());
        }
    }
#endif
}
} // namespace internal

// PUBLIC API:
/** Install signal handler that catches FATAL C-runtime or OS signals
     See the wikipedia site for details http://en.wikipedia.org/wiki/SIGFPE
     See the this site for example usage: http://www.tutorialspoint.com/cplusplus/cpp_signal_handling
     SIGABRT  ABORT (ANSI), abnormal termination
     SIGFPE   Floating point exception (ANSI)
     SIGILL   ILlegal instruction (ANSI)
     SIGSEGV  Segmentation violation i.e. illegal memory reference
     SIGTERM  TERMINATION (ANSI)  */
inline void installCrashHandler()
{
    internal::installSignalHandler();
}

inline void installCrashHandlerOnce()
{
    static std::once_flag signal_install_flag;
    std::call_once(signal_install_flag, [] { internal::installSignalHandler(); });
}

/// Overrides the existing signal handling for custom signals
/// For example: usage of zcmq relies on its own signal handler for SIGTERM
///     so users with zcmq should then use the @ref overrideSetupSignals
///     , likely with the original set of signals but with SIGTERM removed
///
/// call example:
///  g3::overrideSetupSignals({ {SIGABRT, "SIGABRT"}, {SIGFPE, "SIGFPE"},{SIGILL, "SIGILL"},
//                          {SIGSEGV, "SIGSEGV"},});
inline void overrideSetupSignals(const std::map<int, std::string> overrideSignals)
{
    static std::mutex signalLock;
    std::lock_guard<std::mutex> guard(signalLock);
    for (const auto &sig : internal::gSignals)
    {
        internal::restoreSignalHandler(sig.first);
    }

    internal::gSignals = overrideSignals;
    installCrashHandler(); // installs all the signal handling for gSignals
}

/// Probably only needed for unit testing. Resets the signal handling back to default
/// which might be needed in case it was previously overridden
/// The default signals are: SIGABRT, SIGFPE, SIGILL, SIGSEGV, SIGTERM
inline void restoreSignalHandlerToDefault()
{
    overrideSetupSignals(internal::kSignals);
}

} // end namespace sds_logging

