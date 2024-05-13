#pragma once

#include <sisl/logging/logging.h>
#if defined(__linux__)
#include <breakpad/client/linux/handler/exception_handler.h>
#endif

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

#if defined(__linux__)
static bool dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, [[maybe_unused]] void*,
                         bool succeeded) {
    std::cerr << std::endl << "Minidump path: " << descriptor.path() << std::endl;
    return succeeded;
}
#endif

static void bt_dumper([[maybe_unused]] const SignalType signal_number) {
#if defined(__linux__)
    google_breakpad::ExceptionHandler::WriteMinidump(get_base_dir().string(), dumpCallback, nullptr);
#endif
}

static void log_stack_trace(const bool, const SignalType signal_number) { bt_dumper(signal_number); }

} // namespace logging
} // namespace sisl