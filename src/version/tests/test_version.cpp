#include <sisl/version.hpp>

#include <iostream>
#include <boost/preprocessor/stringize.hpp>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <gtest/gtest.h>

using namespace sisl;

SISL_OPTIONS_ENABLE(logging)
SISL_LOGGING_INIT()

void entry() {
    auto ver{version::Semver200_version("0.2.3-rc1+57")};
    sisl::VersionMgr::addVersion("dummy", ver);
}

TEST(entryTest, entry) {
    entry();

    auto dummy_ver = sisl::VersionMgr::getVersion("dummy");
    std::stringstream dummy_ver_str;
    dummy_ver_str << dummy_ver;
    LOGINFO("Dummy ver. {}", dummy_ver_str.str());

    std::stringstream sisl_ver;
    sisl_ver << sisl::VersionMgr::getVersion("sisl");
    LOGINFO("SISL ver. {}", sisl_ver.str());

    EXPECT_EQ(dummy_ver.major(), 0u);
    EXPECT_EQ(dummy_ver.minor(), 2u);
    EXPECT_EQ(dummy_ver.patch(), 3u);
    EXPECT_EQ(dummy_ver.build(), "57");
    EXPECT_EQ(dummy_ver.prerelease(), "rc1");

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
