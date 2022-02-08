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
#pragma once

#include <cstdlib>
#include <string>
#include <typeinfo>

#include <cxxabi.h>

namespace sisl {
template < typename T >
struct DeMangler {
    static std::string name() {
        int status;
        const char* const realname{abi::__cxa_demangle(typeid(T).name(), 0, 0, &status)};
        if (realname) {
            const std::string str{realname};
            std::free(realname);
            return str;
        } else
            return std::string{};
    }
};

} // namespace sisl
