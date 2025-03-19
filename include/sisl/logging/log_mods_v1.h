#pragma once
#include <initializer_list>

namespace sisl {
namespace logging {

class LogModulesV1 {
public:
    LogModulesV1(std::initializer_list< const char* > list) { init_modules(list); }
    LogModulesV1(const LogModulesV1&) = delete;
    LogModulesV1& operator=(const LogModulesV1&) = delete;
    LogModulesV1(LogModulesV1&&) noexcept = delete;
    LogModulesV1& operator=(LogModulesV1&&) noexcept = delete;
    ~LogModulesV1() = default;

private:
    void init_modules(std::initializer_list< const char* > mods_list);
};
} // namespace logging
} // namespace sisl

#define MODLEVELDEC(r, _, module)                                                                                      \
    extern "C" {                                                                                                       \
    extern spdlog::level::level_enum BOOST_PP_CAT(module_level_, module);                                              \
    }
MODLEVELDEC(_, _, base)

#define MODLEVELDEF(r, l, module)                                                                                      \
    extern "C" {                                                                                                       \
    __attribute__((visibility("default"))) spdlog::level::level_enum BOOST_PP_CAT(module_level_, module){l};           \
    }

#define MOD_LEVEL_STRING(r, _, module) BOOST_PP_STRINGIZE(module),

#define SISL_LOGGING_DECL(...)                                                                                         \
    BOOST_PP_SEQ_FOR_EACH(MODLEVELDEC, spdlog::level::level_enum::off, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

#define SISL_LOGGING_DEF(...)                                                                                          \
    BOOST_PP_SEQ_FOR_EACH(MODLEVELDEF, spdlog::level::level_enum::err, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

#define SISL_LOGGING_INIT(...)                                                                                         \
    sisl::logging::LogModulesV1 s_init_enabled_mods{                                                                   \
        BOOST_PP_SEQ_FOR_EACH(MOD_LEVEL_STRING, , BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))};

#define REGISTER_LOG_MODS(...)                                                                                         \
    {}
#define REGISTER_LOG_MOD(name)                                                                                         \
    {}
#define LEVELCHECK(mod, lvl) (module_level_##mod <= (lvl))