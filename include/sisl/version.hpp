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
#include <semver200.h>
#include <vector>

namespace sisl::VersionMgr {
using mod_info_t = std::pair< std::string, version::Semver200_version >;
[[maybe_unused]]
extern version::Semver200_version getVersion(const std::string& name);
[[maybe_unused]]
extern std::vector< mod_info_t > getVersions();
[[maybe_unused]]
extern void addVersion(const std::string& name, const version::Semver200_version& ver);

} // namespace sisl::VersionMgr
