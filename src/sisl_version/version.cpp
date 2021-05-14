//
// Version specific calls
//

#include "version.hpp"

namespace sisl {

extern const version::Semver200_version get_version() {
    return version::Semver200_version(PACKAGE_VERSION);
}

} // namespace sisl
