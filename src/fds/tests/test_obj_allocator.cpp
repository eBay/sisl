#include <cstdint>
#include <iostream>

#include "logging/logging.h"
#include "options/options.h"

#include "obj_allocator.hpp"

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

int main(int argc, char** argv) {
    Node< uint64_t >* const ptr1{sisl::ObjectAllocator< Node< uint64_t > >::make_object(~static_cast< uint64_t >(0))};
    std::cout << "ptr1 = " << static_cast< const void* >(ptr1) << " Id = " << ptr1->get_id() << std::endl;
    sisl::ObjectAllocator< Node< uint64_t > >::deallocate(ptr1);
}
