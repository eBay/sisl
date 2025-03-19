#include <sisl/logging/logging.h>

void example_decl() {
    REGISTER_LOG_MOD(my_module)
    LOGINFOMOD(my_module, "Example def!");
}
