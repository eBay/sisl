#include <sisl/version.hpp>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
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

    std::stringstream dummy_ver;
    dummy_ver << sisl::VersionMgr::getVersion("dummy");
    LOGINFO("Dummy ver. {}", dummy_ver.str());

    std::stringstream sisl_ver;
    sisl_ver << sisl::VersionMgr::getVersion("sisl");
    LOGINFO("SISL ver. {}", sisl_ver.str());

    EXPECT_EQ(dummy_ver.str(), sisl_ver.str());

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
