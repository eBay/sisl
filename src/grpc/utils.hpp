/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
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

#include <string>
#include <fstream>

namespace sisl {

static bool get_file_contents(const std::string& file_name, std::string& contents) {
    try {
        std::ifstream f(file_name);
        std::string buffer(std::istreambuf_iterator< char >{f}, std::istreambuf_iterator< char >{});
        contents = buffer;
        return !contents.empty();
    } catch (...) {}
    return false;
}

} // namespace sisl::grpc
