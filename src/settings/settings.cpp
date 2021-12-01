/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Harihara Kadayam
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
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include <boost/algorithm/string.hpp>

#include "options/options.h"

#include "settings.hpp"

SISL_OPTION_GROUP(config,
                  (config_path, "", "config_path", "Path to dynamic config of app", cxxopts::value< std::string >(),
                   ""),
                  (override_config, "", "override_config", "Config option to override after load",
                   ::cxxopts::value< std::vector< std::string > >(), "configs [...]"))

namespace sisl {
static nlohmann::json kv_path_to_json(const std::vector< std::string >& paths, const std::string& val) {
    std::string json_str;
    for (auto& p : paths) {
        json_str += "{\"" + p + "\":";
    }

    if (std::find_if(std::cbegin(val), std::cend(val), [](const char c) { return !std::isdigit(c); }) ==
        std::cend(val)) {
        json_str += val;
    } else {
        json_str += fmt::format("\"{}\"", val);
    }
    json_str += std::string(paths.size(), '}');

    return nlohmann::json::parse(json_str);
}

SettingsFactoryRegistry::SettingsFactoryRegistry() {
    if (SISL_OPTIONS.count("override_config") != 0) {
        const auto cfgs{SISL_OPTIONS["override_config"].as< std::vector< std::string > >()};
        for (const auto& cfg : cfgs) {
            // Get the entire path along with module name and its value
            std::vector< std::string > kv;
            boost::split(kv, cfg, boost::is_any_of(":="));
            if (kv.size() < 2) { continue; }

            // Split this and convert to a json string which json library can parse. I am sure
            // there are cuter ways to do this, but well someother time....
            std::vector< std::string > paths;
            boost::split(paths, kv[0], boost::is_any_of("."));
            if (paths.size() < 2) { continue; }
            auto schema_name{std::move(paths.front())};
            paths.erase(std::begin(paths));

            auto j = kv_path_to_json(paths, kv[1]); // Need a copy constructor here.
            const auto it{m_override_cfgs.find(schema_name)};
            if (it != std::cend(m_override_cfgs)) {
                it->second.merge_patch(j);
            } else {
                m_override_cfgs.emplace(std::move(schema_name), std::move(j));
            }
        }
    }
}

void SettingsFactoryRegistry::register_factory(const std::string& name, SettingsFactoryBase* const f) {
    if (SISL_OPTIONS.count("config_path") == 0) { return; }

    const auto config_file{fmt::format("{}/{}.json", SISL_OPTIONS["config_path"].as< std::string >(), name)};
    {
        std::unique_lock lg{m_mtx};
        f->set_config_file(config_file);

        // First create the default file
        if (!std::filesystem::is_regular_file(std::filesystem::status(config_file))) {
            LOGWARN("Config file '{}'  does not exist Saving default to that file", config_file);
            f->save();
        }

        // Check if we have overridden config for this schema name
        const auto it{m_override_cfgs.find(name)};
        if (it != std::cend(m_override_cfgs)) {
            // Read the json file and push the overridden config inside
            assert(std::filesystem::is_regular_file(std::filesystem::status(config_file)));
            LOGINFO("Settings schema {} has some overridden parameters, applying that", name);

            nlohmann::json config_js;
            {
                std::ifstream ifs{config_file};
                config_js = nlohmann::json::parse(ifs);
            }
            config_js.merge_patch(it->second); // Merge the overridden json with current config json
            {
                std::ofstream ofs{config_file};
                ofs << config_js.dump(2);
            }
        }

        // Now load the config for this schema
        LOGINFO("Loading the settings schema for {} from file: {}", name, config_file);
        f->load();
        m_factories.insert(std::make_pair(name, f));
    }
}

void SettingsFactoryRegistry::unregister_factory(const std::string& name) {
    std::unique_lock lg{m_mtx};
    m_factories.erase(name);
}

bool SettingsFactoryRegistry::reload_all() {
    bool ret{false};
    std::shared_lock lg{m_mtx};
    for (auto& [name, factory] : m_factories) {
        if (factory->reload()) { ret = true; }
    }
    return ret;
}

void SettingsFactoryRegistry::save_all() {
    std::shared_lock lg{m_mtx};
    for (auto& [name, factory] : m_factories) {
        factory->save();
    }
}

nlohmann::json SettingsFactoryRegistry::get_json() const {
    nlohmann::json j;
    std::shared_lock lg{m_mtx};
    for (auto& [name, factory] : m_factories) {
        j[name] = nlohmann::json::parse(factory->get_json());
    }
    return j;
}
} // namespace sisl
