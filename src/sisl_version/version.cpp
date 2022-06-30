/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Brian Szymd
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
#include "version.hpp"
#include "logging/logging.h"
#include <cassert>

namespace sisl {

VersionMgr* VersionMgr::m_instance = nullptr;
std::once_flag VersionMgr::m_init_flag;

VersionMgr::~VersionMgr() {
    for (auto it = m_version_map.begin(); m_version_map.end() != it; ++it) {
        semver_free(&it->second);
    }
}

void VersionMgr::createAndInit() {
    m_instance = new VersionMgr();
    auto& version = m_instance->m_version_map["sisl"];
    auto const ret = semver_parse(PACKAGE_VERSION, &version);
    RELEASE_ASSERT_EQ(0, ret, "Version could not be parsed: {}", PACKAGE_VERSION);
}

VersionMgr* VersionMgr::getInstance() {
    std::call_once(m_init_flag, &VersionMgr::createAndInit);
    return m_instance;
}

semver_t* VersionMgr::getVersion(const std::string& name) {
    auto ver_info{VersionMgr::getInstance()};
    std::unique_lock l{ver_info->m_mutex};
    auto it{ver_info->m_version_map.find(name)};
    assert(it != ver_info->m_version_map.end());
    return &it->second;
}

std::vector< modinfo > VersionMgr::getVersions() {
    std::vector< modinfo > res;
    auto ver_info{VersionMgr::getInstance()};
    std::unique_lock l{ver_info->m_mutex};
    res.insert(res.end(), ver_info->m_version_map.begin(), ver_info->m_version_map.end());
    return res;
}

void VersionMgr::addVersion(const std::string& name, const semver_t& ver) {
    auto ver_info{VersionMgr::getInstance()};
    std::unique_lock l{ver_info->m_mutex};
    auto it{ver_info->m_version_map.find(name)};
    if (it == ver_info->m_version_map.end()) { ver_info->m_version_map[name] = ver; }
}

} // namespace sisl
