#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <sisl/utility/thread_factory.hpp>

SISL_LOGGING_INIT(my_module)

SISL_OPTIONS_ENABLE(logging)

extern void example_decl();

using namespace std::chrono_literals;

int main(int argc, char** argv) {
    SISL_OPTIONS_LOAD(argc, argv, logging)
    sisl::logging::SetLogger(std::string(argv[0]));
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    LOGTRACE("Trace");
    LOGDEBUG("Debug");
    LOGINFO("Info");
    LOGWARN("Warning");
    LOGERROR("Error");
    LOGCRITICAL("Critical");

    auto j_thread = std::jthread([](std::stop_token stoken) {
        example_decl();
        while (!stoken.stop_requested()) {
            LOGWARNMOD(my_module, "Sleeping...");
            std::this_thread::sleep_for(1500ms);
        }
        LOGINFOMOD(my_module, "Waking...");
        std::this_thread::sleep_for(1500ms);
    });
    sisl::name_thread(j_thread, "example_decl");
    std::this_thread::sleep_for(300ms);
    auto stop_source = j_thread.get_stop_source();

    auto custom_logger =
        sisl::logging::CreateCustomLogger("test_package", "_custom", false /*stdout*/, true /*stderr*/);
    LOGINFOMOD_USING_LOGGER(my_module, custom_logger, "hello world");
    DEBUG_ASSERT(true, "Always True");
    RELEASE_ASSERT(stop_source.request_stop(), "Should be!");

    return 0;
}
