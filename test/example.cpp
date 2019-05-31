#include <condition_variable>
#include <iostream>
#include <mutex>
#include <logging.h>
//#include <stack_trace.h>
#include <sds_options/options.h>

SDS_LOGGING_INIT(my_module)

SDS_OPTIONS_ENABLE(logging)

//SDS_LOG_LEVEL(my_module, spdlog::level::level_enum::error)

void func() {
        LOGINFO("Thread func started");
        auto i = 0;
        while (i < 3) {
                LOGINFO("Thread func {}th iteration", i+1);
                sleep(3);
                ++i;
        }
}

int main(int argc, char** argv) {
   SDS_OPTIONS_LOAD(argc, argv, logging)
   sds_logging::SetLogger(std::string(argv[0]));
   spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

   SDS_LOG_LEVEL(base, spdlog::level::level_enum::trace);
   sds_logging::install_crash_handler();

   std::thread t(func);
   sleep(1);
   std::cout << "spdlog level base = " << module_level_base << "\n";
   LOGTRACE("Trace");
   LOGDEBUG("Debug");
   LOGINFO("Info");
   LOGWARN("Warning");
   LOGERROR("Error");
   LOGCRITICAL("Critical");

   SDS_LOG_LEVEL(my_module, spdlog::level::level_enum::info);
   LOGINFOMOD(my_module, "Enabled Module Logger");
   LOGTRACEMOD(my_module, "Trace Module");

   //backtrace_unwind();
   sds_logging::log_stack_trace();

#if 0
   RELEASE_ASSERT_EQ(argc, 2, "I can't run without proper arguments in release build");
   RELEASE_ASSERT_EQ(argc, 2);
   DEBUG_ASSERT_EQ(argc, 2, "I can't run without proper arguments in debug build, need {} args", 2);
   RELEASE_ASSERT_EQ(argc, 2, "I can't run without proper arguments in release build, need {} args", 2);
   int* x = nullptr; *x = 5;
#endif

   t.join();
   return 0;
}
