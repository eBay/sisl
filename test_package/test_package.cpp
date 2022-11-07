#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

SISL_LOGGING_INIT(my_module)

SISL_OPTIONS_ENABLE(logging)

extern void example_decl();

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

   example_decl();

   auto custom_logger = sisl::logging::CreateCustomLogger("test_package", "_custom", false /*stdout*/, true /*stderr*/);
   LOGINFOMOD_USING_LOGGER(my_module, custom_logger, "hello world");
   DEBUG_ASSERT(true, "Always True");
   return 0;
}
