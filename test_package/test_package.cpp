#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/utility/thread_factory.hpp>
#include <breakpad/client/linux/handler/exception_handler.h>

SISL_LOGGING_INIT(my_module)

SISL_OPTIONS_ENABLE(logging)

extern void example_decl();

using namespace std::chrono_literals;

static bool dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor,
void* context, bool succeeded) {
  printf("Dump path: %s\n", descriptor.path());
  return succeeded;
}

[[ maybe_unused ]]
void crash() { volatile int* a = (int*)(NULL); *a = 1; }

int main(int argc, char** argv) {
    google_breakpad::MinidumpDescriptor descriptor("./");
    google_breakpad::ExceptionHandler eh(descriptor, NULL, dumpCallback, NULL, true, -1);

    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger(std::string(argv[0]));
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    LOGTRACE("Trace");
    LOGDEBUG("Debug");
    LOGINFO("Info");
    LOGWARN("Warning");
    LOGERROR("Error");
    LOGCRITICAL("Critical");

    auto _thread = std::thread([]() {
        LOGWARNMOD(my_module, "Sleeping...");
        std::this_thread::sleep_for(1500ms);
        LOGINFOMOD(my_module, "Waking...");
        std::this_thread::sleep_for(1500ms);
    });
    sisl::name_thread(_thread, "example_decl");
    std::this_thread::sleep_for(300ms);

    auto custom_logger =
        sisl::logging::CreateCustomLogger("test_package", "_custom", false /*stdout*/, true /*stderr*/);
    LOGINFOMOD_USING_LOGGER(my_module, custom_logger, "hello world");
    DEBUG_ASSERT(true, "Always True");
    _thread.join();
    //crash();
    return 0;
}
