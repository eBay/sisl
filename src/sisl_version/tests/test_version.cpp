#include <version.hpp>
#include "logging/logging.h"
#include "options/options.h"
#include <gtest/gtest.h>
#include <iostream>

using namespace sisl;

SISL_OPTIONS_ENABLE(logging)
SISL_LOGGING_INIT(test_version)

void entry() {
    auto ver{version::Semver200_version(PACKAGE_VERSION)};
    sisl::VersionMgr::addVersion("dummy", ver);
}

TEST(entryTest, entry) {
    entry();

    const std::string dummy_ver{fmt::format("{0}", sisl::VersionMgr::getVersion("dummy"))};
    LOGINFO("Dummy ver. {}", dummy_ver);

    const std::string sisl_ver{fmt::format("{0}", sisl::VersionMgr::getVersion("sisl"))};
    LOGINFO("SISL ver. {}", sisl_ver);

    EXPECT_EQ(dummy_ver, sisl_ver);

    auto versions{sisl::VersionMgr::getVersions()};
    EXPECT_EQ((int)versions.size(), 2);
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging);
    sisl::logging::SetLogger("test_version");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");
    return RUN_ALL_TESTS();
}
