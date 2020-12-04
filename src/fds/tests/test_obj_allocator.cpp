#include <cstdint>
#include <iostream>

#include <sds_logging/logging.h>
#include <sds_options/options.h>

#include "obj_allocator.hpp"

SDS_LOGGING_INIT(HOMESTORE_LOG_MODS)
THREAD_BUFFER_INIT

using namespace sisl;
using namespace std;

namespace {
template < typename T >
class Node {
public:
    Node(T id) { m_id = id; }

    T get_id() { return m_id; }

    ~Node() { std::cout << "Destructor of Node " << m_id << " called\n"; }

private:
    T m_id;
};
} // namespace

int main(int argc, char** argv) {
    Node< uint64_t >* ptr1 = sisl::ObjectAllocator< Node< uint64_t > >::make_object((uint64_t)-1);
    std::cout << "ptr1 = " << (void*)ptr1 << " Id = " << ptr1->get_id() << std::endl;
    sisl::ObjectAllocator< Node< uint64_t > >::deallocate(ptr1);
}
