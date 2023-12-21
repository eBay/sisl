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
#include <cstdint>
#include <iostream>

#include "sisl/logging/logging.h"
#include "sisl/options/options.h"

#include "sisl/fds/obj_allocator.hpp"

SISL_LOGGING_INIT(HOMESTORE_LOG_MODS)

using namespace sisl;
using namespace std;

namespace {
template < typename T >
class Node {
public:
    Node(const T& id) { m_id = id; }

    const T& get_id() const { return m_id; }

    ~Node() { std::cout << "Destructor of Node " << m_id << " called\n"; }

private:
    T m_id;
};
} // namespace

SISL_OPTIONS_ENABLE(logging)

int main(int argc, char* argv[]) {
    SISL_OPTIONS_LOAD(argc, argv, logging);
    Node< uint64_t >* const ptr1{sisl::ObjectAllocator< Node< uint64_t > >::make_object(~static_cast< uint64_t >(0))};
    std::cout << "ptr1 = " << static_cast< const void* >(ptr1) << " Id = " << ptr1->get_id() << std::endl;
    sisl::ObjectAllocator< Node< uint64_t > >::deallocate(ptr1);
}
