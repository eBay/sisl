#include <sisl/logging/logging.h>

SISL_LOGGING_DECL(my_module)

void example_decl() {
  LOGINFOMOD(my_module, "Example def!");
}
