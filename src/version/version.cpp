/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Brian Szmyd
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
#include <sisl/version.hpp>

#include <cassert>
#include <mutex>
#include <unordered_map>
#include <boost/preprocessor/stringize.hpp>

namespace sisl::VersionMgr {

static std::mutex k_mutex;
static std::unordered_map< std::string, version::Semver200_version > k_version_map;

static void createAndInit() {
    static std::once_flag k_init_flag;
    std::call_once(k_init_flag, []() {
        if (auto [it, happened] = k_version_map.emplace(std::make_pair("sisl", version::Semver200_version())); happened)
            it->second = version::Semver200_version(BOOST_PP_STRINGIZE(PACKAGE_VERSION));
    });
}

version::Semver200_version getVersion(const std::string& name) {
    auto lg = std::scoped_lock< std::mutex >(k_mutex);
    createAndInit();
    if (auto it = k_version_map.find(name); k_version_map.end() != it) return it->second;
    return version::Semver200_version();
}

std::vector< mod_info_t > getVersions() {
    std::unique_lock l{k_mutex};
    createAndInit();
    std::vector< mod_info_t > res;
    res.insert(res.end(), k_version_map.begin(), k_version_map.end());
    return res;
}

void addVersion(const std::string& name, const version::Semver200_version& ver) {
    std::unique_lock l{k_mutex};
    createAndInit();
    auto it{k_version_map.find(name)};
    if (it == k_version_map.end()) { k_version_map[name] = ver; }
}

} // namespace sisl::VersionMgr
