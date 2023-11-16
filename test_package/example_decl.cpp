#include <sisl/logging/logging.h>

SISL_LOGGING_DEF(my_module)

void example_decl() {
  LOGINFOMOD(my_module, "Example def!");
}
