//
// Version specific calls
//

#include "version.hpp"
#include <cassert>

namespace sisl {

VersionMgr* VersionMgr::m_instance = nullptr;
std::once_flag VersionMgr::m_init_flag;

void VersionMgr::createAndInit() {
    m_instance = new VersionMgr();
    auto ver{version::Semver200_version(PACKAGE_VERSION)};
    m_instance->m_version_map["sisl"] = ver;
}

VersionMgr* VersionMgr::getInstance() {
    std::call_once(m_init_flag, &VersionMgr::createAndInit);
    return m_instance;
}

version::Semver200_version VersionMgr::getVersion(const std::string& name) {
    auto ver_info{VersionMgr::getInstance()};
    std::unique_lock l{ver_info->m_mutex};
    auto it{ver_info->m_version_map.find(name)};
    assert(it != ver_info->m_version_map.end());
    return it->second;
}

std::vector< modinfo > VersionMgr::getVersions() {
    std::vector< modinfo > res;
    auto ver_info{VersionMgr::getInstance()};
    std::unique_lock l{ver_info->m_mutex};
    res.insert(res.end(), ver_info->m_version_map.begin(), ver_info->m_version_map.end());
    return res;
}

void VersionMgr::addVersion(const std::string& name, const version::Semver200_version& ver) {
    auto ver_info{VersionMgr::getInstance()};
    std::unique_lock l{ver_info->m_mutex};
    auto it{ver_info->m_version_map.find(name)};
    if (it == ver_info->m_version_map.end()) { ver_info->m_version_map[name] = ver; }
}

} // namespace sisl
