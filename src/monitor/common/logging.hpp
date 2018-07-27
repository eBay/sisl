//
// Created by Finkelstein, Yuri on 1/19/18.
//

#ifndef MONSTORDATABASE_LOGGING_HPP
#define MONSTORDATABASE_LOGGING_HPP

/*
 * Logging: We use GLog for logging the data. We customized the Google GLog, so that it can accept the
 * user provided module name instead of defaulting always to a filename. This way we can logically split
 * the module, that cut across multiple files.
 *
 * In order to log a message, suggested approach is
 *
 * DCVLOG(<module_name>, <verbose_level>) << "Your log message if built in debug mode";
 * CVLOG(<module_name>, <verbose_level>) << "Your debug and release log message";
 * LOG(ERROR|WARNING|FATAL|INFO) << "Your unconditional log message";
 * DLOG(ERROR|WARNING|FATAL|INFO) << "Your unconditional log message only in debug";
 *
 * LOG(DFATAL) << "Your message and assert in Debug mode, treats as LOG(ERROR) in release mode".
 *
 * Please avoid using DVLOG and VLOG directly as it puts module name as filename, which violates the general
 * purpose nature of this. Please generously use LOG(DFATAL) instead of assert, since in production release
 * it can log message instead of silently discarding it (unless the assert check will affect performance)
 *
 * Please pick a module name listed below. If it does not suit you, add one and use that name while logging.
 * Suggested verbose level is 1 - 6. While having a verbose level per module, gives the flexibility to use
 * different verbose standards for different modules, in production, its always convenient to stick to
 * similar verbosity across modules as well.
 *
 * How to enable logging:
 * Set environment variables
 *  GLOG_v=<common verbose level>
 *  GLOG_vmodule="<module name1>=<overridden verbose level>,<module name2>=<overridden verbose level>"
 *
 *  Example: GLOG_v=0, GLOG_vmodule="txn=5,op=4,network=1" ...
 *
 * How to set the level dynamically:
 * TODO: Work in progress...
 *
 */
#include <glog/logging.h>

#define VMODULE_CMD cmd
#define VMODULE_SETTINGS settings
#define VMODULE_TXN txn
#define VMODULE_OP op
#define VMODULE_CLEANUP cleanup
#define VMODULE_ADMIN admin
#define VMODULE_BSON mutablebson
#define VMODULE_METRICS metrics
#define VMODULE_MONGO_TRANSP mongo
#define VMODULE_SC state_controller
#define VMODULE_CDC cdc
#define VMODULE_CALLDATA calldata

// NOTE: If new modules are introduced, add it into above #define and also remember to add to the list below.
#define FOREACH_VMODULE(method)                                                                                        \
    method(VMODULE_CMD) method(VMODULE_TXN) method(VMODULE_OP) method(VMODULE_CLEANUP) method(VMODULE_ADMIN)           \
        method(VMODULE_BSON) method(VMODULE_METRICS) method(VMODULE_MONGO_TRANSP) method(VMODULE_SC)                   \
            method(VMODULE_SETTINGS) method(VMODULE_CDC) method(VMODULE_CALLDATA)

#define VMODULE_STR_INTERNAL(m) #m
#define VMODULE_STR(m) VMODULE_STR_INTERNAL(m)
#define VMODULE_LIST_STR(m) VMODULE_STR_INTERNAL(m),
#define VMODULE_REGISTER_MODULE(m) VLOG_REG_MODULE(m);
#define VMODULE_DECLARE_MODULE(m) VLOG_DECL_MODULE(m);
#define VMODULE_INITIALIZE_MODULE(m)                                                                                   \
    if (google::GetVLOGLevel(VMODULE_STR(m)) == -1) {                                                                  \
        google::SetVLOGLevel(VMODULE_STR(m), getenv("GLOG_v") ? atoi(getenv("GLOG_v")) : 0);                           \
    }

//#define VMODULE_INITIALIZE_MODULE(m) VMODULE_INITIALIZE_MODULE_INTERNAL(m)

#define VMODULE_ALL_LIST FOREACH_VMODULE(VMODULE_LIST_STR)

// Register all modules with Glog subsystem. This creates the global variable for each module
FOREACH_VMODULE(VMODULE_REGISTER_MODULE);

#define CVLOGM(custom_module, verboselevel)                                                                            \
    LOG_IF(INFO, CVLOG_IS_ON(custom_module, verboselevel)) << "[" << VMODULE_STR_INTERNAL(custom_module) << "]"

#define CVLOGMC(custom_module, component, verboselevel)                                                                \
    LOG_IF(INFO, CVLOG_IS_ON(custom_module, verboselevel))                                                             \
        << "[" << VMODULE_STR_INTERNAL(custom_module) << "::" << VMODULE_STR_INTERNAL(component) << "]"                \
        << "===>"

#define CLOGWARNING(custom_module, component)                                                                          \
    LOG(WARNING) << "[" << VMODULE_STR_INTERNAL(custom_module) << "::" << VMODULE_STR_INTERNAL(component) << "]"       \
                 << "===>"

#define CLOGERROR(custom_module, component)                                                                            \
    LOG(ERROR) << "[" << VMODULE_STR_INTERNAL(custom_module) << "::" << VMODULE_STR_INTERNAL(component) << "]"         \
               << "===>"

//#include <gsl/gsl>
// add condition text to diagnostic message
//#undef Expects
//#undef Ensures
//#define Expects(cond) GSL_CONTRACT_CHECK("Precondition "#cond, cond)
//#define Ensures(cond) GSL_CONTRACT_CHECK("Postcondition "#cond, cond)

#endif // MONSTORDATABASE_LOGGING_HPP
