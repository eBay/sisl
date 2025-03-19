/*********************************************************************************
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#pragma once

#define SPDLOG_FUNCTION __PRETTY_FUNCTION__
#define SPDLOG_NO_NAME

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <filesystem>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h> // NOTE: There is an ordering dependecy on this header and fmt headers below

#define MOD_DECLTYPE(name) decltype(BOOST_PP_CAT(BOOST_PP_STRINGIZE(name), _lmtstr))

#define REGISTER_LOG_MOD(name)                                                                                         \
    {                                                                                                                  \
        using namespace sisl::logging;                                                                                 \
        auto& mod = ModuleName< MOD_DECLTYPE(name) >::instance();                                                      \
        sisl::logging::LogModulesV2::instance().register_module(&mod);                                                 \
    }

#define _REGISTER_LOG_MOD_MACRO(r, _, module) REGISTER_LOG_MOD(module)
#define REGISTER_LOG_MODS(...) BOOST_PP_SEQ_FOR_EACH(_REGISTER_LOG_MOD_MACRO, , BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))
#define LEVELCHECK(mod, lvl) (ModuleName< MOD_DECLTYPE(mod) >::instance().get_level() <= (lvl))

namespace sisl {
namespace logging {

class ModuleBase {
public:
    ModuleBase() = default;
    virtual std::string get_name() const = 0;
    virtual void set_level(spdlog::level::level_enum level) = 0;
    virtual spdlog::level::level_enum get_level() const = 0;
    virtual ~ModuleBase() = default;
};

class LogModulesV2 {
public:
    static constexpr spdlog::level::level_enum k_default_level{spdlog::level::level_enum::err};

    static LogModulesV2& instance() {
        static LogModulesV2 s_inst{};
        return s_inst;
    }

    void register_module(ModuleBase* mod) {
        std::unique_lock lock{m_mutex};
        // Add to registered modules with default level
        m_registered_modules.emplace(mod->get_name(), mod);

        // If this is already requested to setup a specific level, update the registered module with that level and
        // remove it from requested modules (it is no longer pending)
        auto const it = m_requested_modules.find(mod->get_name());
        if (it != m_requested_modules.end()) {
            mod->set_level(it->second);
            m_requested_modules.erase(it);
        } else {
            mod->set_level(k_default_level);
        }
    }

    void set_module_level(const std::string& name, spdlog::level::level_enum level) {
        std::unique_lock lock{m_mutex};
        // If this module is already registered, directly update the level
        auto it = m_registered_modules.find(name);
        if (it != m_registered_modules.end()) {
            it->second->set_level(level);
        } else {
            // Not registered yet, keep in request list and once module is registered, it will taken from this list
            m_requested_modules[name] = level;
        }
    }

    spdlog::level::level_enum get_module_level(const std::string& module_name) {
        std::unique_lock lock{m_mutex};
        if (auto it = m_registered_modules.find(module_name); it != m_registered_modules.end()) {
            return it->second->get_level();
        } else if (auto it2 = m_requested_modules.find(module_name); (it2 != m_requested_modules.end())) {
            return it2->second;
        }

        return k_default_level;
    }

    std::unordered_map< std::string, spdlog::level::level_enum > get_all_module_levels() {
        std::unique_lock lock{m_mutex};
        std::unordered_map< std::string, spdlog::level::level_enum > ret;
        ret = m_requested_modules;
        for (auto& [name, mod] : m_registered_modules) {
            ret[name] = mod->get_level();
        }
        return ret;
    }

    void set_all_module_levels(spdlog::level::level_enum level) {
        std::unique_lock lock{m_mutex};
        for (auto& [name, mod] : m_registered_modules) {
            mod->set_level(level);
        }

        for (auto& [name, _] : m_requested_modules) {
            m_requested_modules[name] = level;
        }
    }

private:
    LogModulesV2() = default;
    std::mutex m_mutex;
    std::unordered_map< std::string, ModuleBase* > m_registered_modules;
    std::unordered_map< std::string, spdlog::level::level_enum > m_requested_modules;
};

} // namespace logging
} // namespace sisl

template < char... chars >
using log_mod_tstring = std::integer_sequence< char, chars... >;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wgnu-string-literal-operator-template"
#endif
template < typename T, T... chars >
constexpr log_mod_tstring< chars... > operator""_lmtstr() {
    return {};
}
#pragma GCC diagnostic pop

template < typename >
class ModuleName;

template < char... elements >
class ModuleName< log_mod_tstring< elements... > > : public sisl::logging::ModuleBase {
public:
    ModuleName(const ModuleName&) = delete;
    ModuleName(ModuleName&&) noexcept = delete;
    ModuleName& operator=(const ModuleName&) = delete;
    ModuleName& operator=(ModuleName&&) noexcept = delete;

    static ModuleName& instance() {
        static ModuleName inst{};
        return inst;
    }

    bool is_registered() const { return m_registered; }
    spdlog::level::level_enum get_level() const override { return m_level; }
    void set_level(spdlog::level::level_enum level) override { m_level = level; }
    std::string get_name() const override { return std::string(s_name); }

private:
    ModuleName() = default;

private:
    static constexpr char s_name[sizeof...(elements) + 1] = {elements..., '\0'};
    spdlog::level::level_enum m_level{spdlog::level::level_enum::off};
    bool m_registered{false};
};