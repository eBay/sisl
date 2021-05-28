/*
 * Created by Sounak Gupta on May-12 2021
 *
 */
#pragma once
#include <semver/semver200.h>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace sisl {

typedef std::pair<std::string, version::Semver200_version> modinfo;

class VersionMgr {
private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, version::Semver200_version> m_version_map;

    VersionMgr() = default;

    static VersionMgr* m_instance;
    static std::once_flag m_init_flag;
    static void createAndInit();

public:
    VersionMgr(VersionMgr const&) = delete;
    void operator=(VersionMgr const&) = delete;

    static VersionMgr* getInstance();
    static version::Semver200_version getVersion(const std::string& name);
    static std::vector<modinfo> getVersions();
    static void addVersion(const std::string& name, const version::Semver200_version& ver);
};

} // namespace sisl
