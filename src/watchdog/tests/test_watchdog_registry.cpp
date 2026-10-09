#include <sisl/watchdog/watchdog_registry.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace sisl;
using namespace std::chrono_literals;

namespace {

bool wait_until_failed(WatchdogRegistry const& registry, std::chrono::milliseconds budget) {
    auto const deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!registry.failures().empty()) { return true; }
        std::this_thread::sleep_for(5ms);
    }
    return !registry.failures().empty();
}

} // namespace

TEST(WatchdogRegistry, EmptyRegistryHasNoFailures) {
    WatchdogRegistry registry;
    EXPECT_TRUE(registry.failures().empty());
}

TEST(WatchdogRegistry, DeadlineNotFailedBeforeLimit) {
    WatchdogRegistry registry;
    auto wd = registry.add_deadline("gc.iteration", 5s);
    (void)wd;
    EXPECT_TRUE(registry.failures().empty());
}

TEST(WatchdogRegistry, DeadlineFailsAfterLimitAndClearsOnDrop) {
    WatchdogRegistry registry;
    {
        auto wd = registry.add_deadline("gc.iteration", 20ms);
        (void)wd;
        EXPECT_TRUE(wait_until_failed(registry, 500ms));
        auto const failures = registry.failures();
        ASSERT_EQ(failures.size(), 1u);
        EXPECT_EQ(failures[0].name, "gc.iteration");
        EXPECT_NE(failures[0].details.find("last seen"), std::string::npos);
        EXPECT_EQ(failures[0].severity, WatchdogSeverity::critical);
    }
    EXPECT_TRUE(registry.failures().empty());
}

TEST(WatchdogRegistry, LeaseKickExtendsDeadline) {
    WatchdogRegistry registry;
    auto lease = registry.add_lease("heartbeat.loop", 80ms);
    std::this_thread::sleep_for(20ms);
    lease->kick();
    EXPECT_TRUE(registry.failures().empty());
    EXPECT_TRUE(wait_until_failed(registry, 500ms));
    ASSERT_EQ(registry.failures().size(), 1u);
    EXPECT_EQ(registry.failures()[0].name, "heartbeat.loop");
}

TEST(WatchdogRegistry, LeaseFailsAndClearsOnDrop) {
    WatchdogRegistry registry;
    {
        auto lease = registry.add_lease("heartbeat.loop", 20ms);
        (void)lease;
        EXPECT_TRUE(wait_until_failed(registry, 500ms));
        EXPECT_EQ(registry.failures().size(), 1u);
    }
    EXPECT_TRUE(registry.failures().empty());
}

TEST(WatchdogRegistry, BarkFailsImmediatelyAndClearsOnDrop) {
    WatchdogRegistry registry;
    {
        auto bark = registry.add_bark("raft.system_exit", "exit_code=1");
        (void)bark;
        auto const failures = registry.failures();
        ASSERT_EQ(failures.size(), 1u);
        EXPECT_EQ(failures[0].name, "raft.system_exit");
        EXPECT_EQ(failures[0].details, "exit_code=1");
        EXPECT_EQ(failures[0].severity, WatchdogSeverity::critical);
    }
    EXPECT_TRUE(registry.failures().empty());
}

TEST(WatchdogRegistry, MoveTransfersRegistration) {
    WatchdogRegistry registry;
    std::unique_ptr< BarkWatchdog > moved;
    {
        auto bark = registry.add_bark("raft.system_exit", "exit_code=2");
        moved = std::move(bark);
        EXPECT_EQ(registry.failures().size(), 1u);
    }
    EXPECT_EQ(registry.failures().size(), 1u);
    moved.reset();
    EXPECT_TRUE(registry.failures().empty());
}

TEST(WatchdogRegistry, MultipleCriticalFailures) {
    WatchdogRegistry registry;
    auto bark = registry.add_bark("raft.system_exit", "exit_code=1");
    auto wd = registry.add_deadline("stuck", 20ms);
    (void)bark;
    (void)wd;
    auto const deadline = std::chrono::steady_clock::now() + 500ms;
    while (std::chrono::steady_clock::now() < deadline && registry.failures().size() < 2u) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_EQ(registry.failures().size(), 2u);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
