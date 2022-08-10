/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Author/Developer(s): Sounak Gupta
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
#include <semver.h>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace sisl {

typedef std::pair< std::string, semver_t > modinfo;

class VersionMgr {
private:
    mutable std::mutex m_mutex;
    std::unordered_map< std::string, semver_t > m_version_map;

    VersionMgr() = default;
    ~VersionMgr();

    static VersionMgr* m_instance;
    static std::once_flag m_init_flag;
    static void createAndInit();

public:
    VersionMgr(VersionMgr const&) = delete;
    void operator=(VersionMgr const&) = delete;

    static VersionMgr* getInstance();
    static void clear();
    static semver_t* getVersion(const std::string& name);
    static std::vector< modinfo > getVersions();
    static void addVersion(const std::string& name, const semver_t& ver);
};

} // namespace sisl
