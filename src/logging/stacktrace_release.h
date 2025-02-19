#pragma once

#include <sisl/logging/logging.h>

namespace sisl {
namespace logging {

static bool g_custom_signal_handler_installed{false};
static size_t g_custom_signal_handlers{0};
static bool g_crash_handle_all_threads{true};
static std::mutex g_hdlr_mutex;

static void flush_logs() { // flush all logs
    spdlog::apply_all([&](std::shared_ptr< spdlog::logger > l) {
        if (l) l->flush();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
}

static void bt_dumper([[maybe_unused]] const SignalType ) {
}

static void log_stack_trace(const bool, const SignalType signal_number) { bt_dumper(signal_number); }

} // namespace logging
} // namespace sisl
