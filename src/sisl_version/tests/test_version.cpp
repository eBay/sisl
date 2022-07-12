#include <version.hpp>
#include "logging/logging.h"
#include "options/options.h"
#include <gtest/gtest.h>
#include <iostream>

using namespace sisl;

SISL_OPTIONS_ENABLE(logging)
SISL_LOGGING_INIT(test_version)

void entry() {
    semver_t new_version = {};
    RELEASE_ASSERT_EQ(0, semver_parse(PACKAGE_VERSION, &new_version), "Could not parse version: {}", PACKAGE_VERSION);
    sisl::VersionMgr::addVersion("dummy", new_version);
}

TEST(entryTest, entry) {
    entry();
    char temp_c_string[100] = {'\0'};

    semver_render(sisl::VersionMgr::getVersion("dummy"), temp_c_string);
    const std::string dummy_ver{fmt::format("{0}", temp_c_string)};
    LOGINFO("Dummy ver. {}", dummy_ver);
    temp_c_string[0] = '\0';

    semver_render(sisl::VersionMgr::getVersion("sisl"), temp_c_string);
    const std::string sisl_ver{fmt::format("{0}", temp_c_string)};
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
