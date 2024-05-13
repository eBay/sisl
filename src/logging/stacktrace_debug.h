#pragma once

#include "backtrace.h"
#include <sisl/logging/logging.h>

namespace sisl {
namespace logging {

auto& logget_thread_context = LoggerThreadContext::instance();
auto logger_thread_registry = logget_thread_context.m_logger_thread_registry;

static std::mutex g_mtx_stack_dump_outstanding;
static size_t g_stack_dump_outstanding{0};
static std::condition_variable g_stack_dump_cv;
static std::array< char, max_stacktrace_size() > g_stacktrace_buff;
static bool g_custom_signal_handler_installed{false};
static size_t g_custom_signal_handlers{0};
static bool g_crash_handle_all_threads{true};
static std::mutex g_hdlr_mutex;

constexpr uint64_t backtrace_timeout_ms{4 * backtrace_detail::pipe_timeout_ms};

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
    std::unique_lock logger_lock{logger_thread_registry->m_logger_thread_mutex};
    auto& logger{GetLogger()};
    auto& critical_logger{GetCriticalLogger()};
    size_t thread_count{1};

    const auto dump_thread{[&logger, &critical_logger, &thread_count](const bool signal_thread, const auto thread_id) {
        if (signal_thread) {
            const auto log_failure{[&logger, &critical_logger, &thread_count, &thread_id](const char* const msg) {
                if (logger) {
#ifndef __APPLE__
                    logger->critical("Thread ID: {}, Thread num: {} - {}\n", thread_id, thread_count, msg);
#else
                    logger->critical("Thread num: {} - {}\n", thread_count, msg);
#endif
                    logger->flush();
                }
                if (critical_logger) {
#ifndef __APPLE__
                    critical_logger->critical("Thread ID: {}, Thread num: {} - {}\n", thread_id, thread_count, msg);
#else
                    critical_logger->critical("Thread num: {} - {}\n", thread_count, msg);
#endif
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
#ifndef __APPLE__
            logger->critical("Thread ID: {}, Thread num: {}\n{}", thread_id, thread_count, g_stacktrace_buff.data());
#else
            logger->critical("Thread num: {}\n{}", thread_count, g_stacktrace_buff.data());
#endif
            logger->flush();
        }
        if (critical_logger) {
#ifndef __APPLE__
            critical_logger->critical("Thread ID: {}, Thread num: {}\n{}", thread_id, thread_count,
                                      g_stacktrace_buff.data());
#else
            critical_logger->critical("Thread num: {}\n{}", thread_count, g_stacktrace_buff.data());
#endif
            critical_logger->flush();
        }
    }};

    // First dump this thread context
    dump_thread(false, logger_thread_ctx.m_thread_id);
    ++thread_count;

    // dump other threads
    for (auto* const ctx : logger_thread_registry->m_logger_thread_set) {
        if (ctx == &logger_thread_ctx) { continue; }
        dump_thread(true, ctx->m_thread_id);
        ++thread_count;
    }
}

static void flush_logs() { // flush all logs
    spdlog::apply_all([&](std::shared_ptr< spdlog::logger > l) {
        if (l) l->flush();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
}

static void log_stack_trace(const bool all_threads, const SignalType) {
    if (is_crash_handler_installed() && all_threads) {
        log_stack_trace_all_threads();
    } else {
        // make this static so that no memory allocation is necessary
        static std::array< char, max_stacktrace_size() > buff;
        buff.fill(0);
        [[maybe_unused]] const size_t s{stack_backtrace(buff.data(), buff.size(), true)};
        LOGCRITICAL("\n\n{}", buff.data());
    }
    flush_logs();
}

} // namespace logging
} // namespace sisl