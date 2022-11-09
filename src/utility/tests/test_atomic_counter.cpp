//
// Created by Kadayam, Hari on Sept 25 2019
//

#include <cstdint>
#include <thread>

#include <sisl/logging/logging.h>
#include <sisl/options/options.h>

#include <gtest/gtest.h>

#include "sisl/utility/atomic_counter.hpp"

using namespace sisl;

SISL_LOGGING_INIT(test_atomic_counter)

namespace {
size_t g_num_threads;

struct AtomicCounterTest : public testing::Test {
public:
    AtomicCounterTest() : testing::Test() { LOGINFO("Initializing new AtomicCounterTest class"); }
    AtomicCounterTest(const AtomicCounterTest&) = delete;
    AtomicCounterTest(AtomicCounterTest&&) noexcept = delete;
    AtomicCounterTest& operator=(const AtomicCounterTest&) = delete;
    AtomicCounterTest& operator=(AtomicCounterTest&&) noexcept = delete;
    virtual ~AtomicCounterTest() override = default;

protected:
    void SetUp() override {}
    void TearDown() override {}
};
} // namespace

TEST_F(AtomicCounterTest, TestSetGet) {
    atomic_counter< uint64_t > uac{};
    uac.set(2);
    EXPECT_EQ(uac.get(), static_cast< uint64_t >(2));

    atomic_counter< int64_t > sac{};
    sac.set(2);
    EXPECT_EQ(sac.get(), static_cast< int64_t >(2));
    sac.set(-2);
    EXPECT_EQ(sac.get(), static_cast< int64_t >(-2));
}

TEST_F(AtomicCounterTest, TestIncrement) {
    atomic_counter< uint64_t > uac{};
    EXPECT_EQ(uac.increment(2), static_cast< uint64_t >(2));
    EXPECT_EQ(uac.get(), static_cast< uint64_t >(2));

    atomic_counter< int64_t > sac{};
    EXPECT_EQ(sac.increment(2), static_cast< int64_t >(2));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(2));
    EXPECT_EQ(sac.increment(-4), static_cast< int64_t >(-2));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(-2));
}

TEST_F(AtomicCounterTest, TestIncrementDeath) {
    atomic_counter< uint8_t > uac{255};
    ASSERT_DEBUG_DEATH(uac.increment(1), "");

    atomic_counter< int8_t > sac1{127};
    ASSERT_DEBUG_DEATH(sac1.increment(1), "");
    atomic_counter< int8_t > sac2{-128};
    ASSERT_DEBUG_DEATH(sac2.increment(-1), "");
}

TEST_F(AtomicCounterTest, TestDecrement) {
    atomic_counter< uint64_t > uac{2};
    EXPECT_EQ(uac.decrement(2), static_cast< uint64_t >(0));
    EXPECT_EQ(uac.get(), static_cast< uint64_t >(0));

    atomic_counter< int64_t > sac{};
    EXPECT_EQ(sac.decrement(2), static_cast< int64_t >(-2));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(-2));
    EXPECT_EQ(sac.decrement(-4), static_cast< int64_t >(2));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(2));
}

TEST_F(AtomicCounterTest, TestDecrementDeath) {
    atomic_counter< uint8_t > uac{};
    ASSERT_DEBUG_DEATH(uac.decrement(1), "");

    atomic_counter< int8_t > sac1{-128};
    ASSERT_DEBUG_DEATH(sac1.decrement(1), "");
    atomic_counter< int8_t > sac2{127};
    ASSERT_DEBUG_DEATH(sac2.decrement(-1), "");
}

TEST_F(AtomicCounterTest, TestIncrementEqual) {
    atomic_counter< uint64_t > uac{1};
    EXPECT_FALSE(uac.increment_test_eq(3, 0));
    EXPECT_TRUE(uac.increment_test_eq(3, 2));
    EXPECT_EQ(uac.get(), static_cast< uint64_t >(3));

    atomic_counter< int64_t > sac{1};
    EXPECT_FALSE(sac.increment_test_eq(3, 0));
    EXPECT_TRUE(sac.increment_test_eq(3, 2));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(3));
    EXPECT_TRUE(sac.increment_test_eq(-1, -4));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(-1));
}

TEST_F(AtomicCounterTest, TestIncrementEqualDeath) {
    atomic_counter< uint8_t > uac{255};
    ASSERT_DEBUG_DEATH(uac.increment_test_eq(1, 1), "");

    atomic_counter< int8_t > sac1{127};
    ASSERT_DEBUG_DEATH(sac1.increment_test_eq(1, 1), "");
    atomic_counter< int8_t > sac2{-128};
    ASSERT_DEBUG_DEATH(sac2.increment_test_eq(1, -1), "");
}

TEST_F(AtomicCounterTest, TestDecrementEqual) {
    atomic_counter< uint64_t > uac{3};
    EXPECT_TRUE(uac.decrement_test_eq(1, 2));
    EXPECT_EQ(uac.get(), static_cast< uint64_t >(1));

    atomic_counter< int64_t > sac{3};
    EXPECT_TRUE(sac.decrement_test_eq(1, 2));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(1));
    EXPECT_TRUE(sac.decrement_test_eq(3, -2));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(3));
}

TEST_F(AtomicCounterTest, TestDecrementEqualDeath) {
    atomic_counter< uint8_t > uac{};
    ASSERT_DEBUG_DEATH(uac.decrement_test_eq(1, 1), "");

    atomic_counter< int8_t > sac1{-128};
    ASSERT_DEBUG_DEATH(sac1.decrement_test_eq(1, 1), "");
    atomic_counter< int8_t > sac2{127};
    ASSERT_DEBUG_DEATH(sac2.decrement_test_eq(1, -1), "");
}

TEST_F(AtomicCounterTest, TestIncrementGreaterEqual) {
    atomic_counter< uint64_t > uac{1};
    EXPECT_FALSE(uac.increment_test_ge(2, 0));
    EXPECT_TRUE(uac.increment_test_ge(3, 2));
    EXPECT_EQ(uac.get(), static_cast< uint64_t >(3));
    EXPECT_TRUE(uac.increment_test_ge(3, 1));
    EXPECT_EQ(uac.get(), static_cast< uint64_t >(4));

    atomic_counter< int64_t > sac{1};
    EXPECT_FALSE(sac.increment_test_ge(2, 0));
    EXPECT_TRUE(sac.increment_test_ge(3, 2));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(3));
    EXPECT_TRUE(sac.increment_test_ge(3, 1));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(4));
    EXPECT_TRUE(sac.increment_test_ge(-2, -5));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(-1));
    EXPECT_TRUE(sac.increment_test_ge(-2, -1));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(-2));
}

TEST_F(AtomicCounterTest, TestIncrementGreaterEqualDeath) {
    atomic_counter< uint8_t > uac{255};
    ASSERT_DEBUG_DEATH(uac.increment_test_ge(1, 1), "");

    atomic_counter< int8_t > sac1{127};
    ASSERT_DEBUG_DEATH(sac1.increment_test_ge(1, 1), "");
    atomic_counter< int8_t > sac2{-128};
    ASSERT_DEBUG_DEATH(sac2.increment_test_ge(1, -1), "");
}

TEST_F(AtomicCounterTest, TestIncrementGreaterEqualWithCount) {
    atomic_counter< uint64_t > uac{1};
    EXPECT_FALSE(uac.increment_test_ge_with_count(2, 0).first);
    const auto uresult1{uac.increment_test_ge_with_count(3, 2)};
    EXPECT_TRUE(uresult1.first);
    EXPECT_EQ(uresult1.second, static_cast< uint64_t >(3));
    const auto uresult2{uac.increment_test_ge_with_count(3, 1)};
    EXPECT_TRUE(uresult2.first);
    EXPECT_EQ(uresult2.second, static_cast< uint64_t >(4));

    atomic_counter< int64_t > sac{1};
    EXPECT_FALSE(sac.increment_test_ge_with_count(2, 0).first);
    const auto sresult1{sac.increment_test_ge_with_count(3, 2)};
    EXPECT_TRUE(sresult1.first);
    EXPECT_EQ(sresult1.second, static_cast< int64_t >(3));
    const auto sresult2{sac.increment_test_ge_with_count(3, 1)};
    EXPECT_TRUE(sresult2.first);
    EXPECT_EQ(sresult2.second, static_cast< int64_t >(4));
    const auto sresult3{sac.increment_test_ge_with_count(-2, -5)};
    EXPECT_TRUE(sresult3.first);
    EXPECT_EQ(sresult3.second, static_cast< int64_t >(-1));
    const auto sresult4{sac.increment_test_ge_with_count(-2, -1)};
    EXPECT_TRUE(sresult4.first);
    EXPECT_EQ(sresult4.second, static_cast< int64_t >(-2));
}

TEST_F(AtomicCounterTest, TestIncrementGreaterEqualWithCountDeath) {
    atomic_counter< uint8_t > uac{255};
    ASSERT_DEBUG_DEATH(uac.increment_test_ge_with_count(1, 1), "");

    atomic_counter< int8_t > sac1{127};
    ASSERT_DEBUG_DEATH(sac1.increment_test_ge_with_count(1, 1), "");
    atomic_counter< int8_t > sac2{-128};
    ASSERT_DEBUG_DEATH(sac2.increment_test_ge_with_count(1, -1), "");
}

TEST_F(AtomicCounterTest, TestDecrementLessEqual) {
    atomic_counter< uint64_t > uac{3};
    EXPECT_FALSE(uac.decrement_test_le(2, 0));
    EXPECT_TRUE(uac.decrement_test_le(1, 2));
    EXPECT_EQ(uac.get(), static_cast< uint64_t >(1));
    EXPECT_TRUE(uac.decrement_test_le(1, 1));
    EXPECT_EQ(uac.get(), static_cast< uint64_t >(0));

    atomic_counter< int64_t > sac{3};
    EXPECT_FALSE(sac.decrement_test_le(2, 0));
    EXPECT_TRUE(sac.decrement_test_le(1, 2));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(1));
    EXPECT_TRUE(sac.decrement_test_le(1, 1));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(0));
    EXPECT_TRUE(sac.decrement_test_le(4, -3));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(3));
    EXPECT_TRUE(sac.decrement_test_le(4, -1));
    EXPECT_EQ(sac.get(), static_cast< int64_t >(4));
}

TEST_F(AtomicCounterTest, TestDecrementLessEqualDeath) {
    atomic_counter< uint8_t > uac{};
    ASSERT_DEBUG_DEATH(uac.decrement_test_le(1, 1), "");

    atomic_counter< int8_t > sac1{-128};
    ASSERT_DEBUG_DEATH(sac1.decrement_test_le(1, 1), "");
    atomic_counter< int8_t > sac2{127};
    ASSERT_DEBUG_DEATH(sac2.decrement_test_le(1, -1), "");
}

TEST_F(AtomicCounterTest, TestDecrementLessEqualWithCount) {
    atomic_counter< uint64_t > uac{3};
    EXPECT_FALSE(uac.decrement_test_le(2, 0));
    const auto uresult1{uac.decrement_test_le_with_count(1, 2)};
    EXPECT_TRUE(uresult1.first);
    EXPECT_EQ(uresult1.second, static_cast< uint64_t >(1));
    const auto uresult2{uac.decrement_test_le_with_count(1, 1)};
    EXPECT_TRUE(uresult2.first);
    EXPECT_EQ(uresult2.second, static_cast< uint64_t >(0));

    atomic_counter< int64_t > sac{3};
    EXPECT_FALSE(sac.decrement_test_le(2, 0));
    const auto sresult1{sac.decrement_test_le_with_count(1, 2)};
    EXPECT_TRUE(sresult1.first);
    EXPECT_EQ(sresult1.second, static_cast< int64_t >(1));
    const auto sresult2{sac.decrement_test_le_with_count(1, 1)};
    EXPECT_TRUE(sresult2.first);
    EXPECT_EQ(sresult2.second, static_cast< int64_t >(0));
    const auto sresult3{sac.decrement_test_le_with_count(4, -3)};
    EXPECT_TRUE(sresult3.first);
    EXPECT_EQ(sresult3.second, static_cast< int64_t >(3));
    const auto sresult4{sac.decrement_test_le_with_count(4, -1)};
    EXPECT_TRUE(sresult4.first);
    EXPECT_EQ(sresult4.second, static_cast< int64_t >(4));
}

TEST_F(AtomicCounterTest, TestDecrementLessEqualWithCountDeath) {
    atomic_counter< uint8_t > uac{};
    ASSERT_DEBUG_DEATH(uac.decrement_test_le_with_count(1, 1), "");

    atomic_counter< int8_t > sac1{-128};
    ASSERT_DEBUG_DEATH(sac1.decrement_test_le_with_count(1, 1), "");
    atomic_counter< int8_t > sac2{127};
    ASSERT_DEBUG_DEATH(sac2.decrement_test_le_with_count(1, -1), "");
}

TEST_F(AtomicCounterTest, TestDecrementZero) {
    atomic_counter< uint64_t > uac{2};
    EXPECT_FALSE(uac.decrement_testz(0));
    EXPECT_TRUE(uac.decrement_testz(2));
    EXPECT_EQ(uac.get(), static_cast< uint64_t >(0));

    atomic_counter< int64_t > sac1{2};
    EXPECT_FALSE(sac1.decrement_testz(0));
    EXPECT_TRUE(sac1.decrement_testz(2));
    EXPECT_EQ(sac1.get(), static_cast< int64_t >(0));

    atomic_counter< int64_t > sac2{-2};
    EXPECT_FALSE(sac2.decrement_testz(0));
    EXPECT_TRUE(sac2.decrement_testz(-2));
    EXPECT_EQ(sac2.get(), static_cast< int64_t >(0));
}

TEST_F(AtomicCounterTest, TestDecrementZeroDeath) {
    atomic_counter< uint8_t > uac{};
    ASSERT_DEBUG_DEATH(uac.decrement_testz(1), "");

    atomic_counter< int8_t > sac1{-128};
    ASSERT_DEBUG_DEATH(sac1.decrement_testz(1), "");
    atomic_counter< int8_t > sac2{127};
    ASSERT_DEBUG_DEATH(sac2.decrement_testz(-1), "");
}

TEST_F(AtomicCounterTest, TestZero) {
    atomic_counter< uint64_t > uac{2};
    EXPECT_FALSE(uac.testz());
    uac.decrement(2);
    EXPECT_TRUE(uac.testz());

    atomic_counter< int64_t > sac{-2};
    EXPECT_FALSE(sac.testz());
    sac.increment(2);
    EXPECT_TRUE(sac.testz());
}

TEST_F(AtomicCounterTest, TestEqual) {
    const atomic_counter< uint64_t > uac{2};
    EXPECT_FALSE(uac.test_eq(0));
    EXPECT_TRUE(uac.test_eq(2));

    const atomic_counter< int64_t > sac{-2};
    EXPECT_FALSE(sac.test_eq(0));
    EXPECT_TRUE(sac.test_eq(-2));
}

TEST_F(AtomicCounterTest, TestLessEqual) {
    const atomic_counter< uint64_t > uac{2};
    EXPECT_FALSE(uac.test_le(1));
    EXPECT_TRUE(uac.test_le(2));
    EXPECT_TRUE(uac.test_le(3));

    const atomic_counter< int64_t > sac{-2};
    EXPECT_FALSE(sac.test_le(-3));
    EXPECT_TRUE(sac.test_le(-2));
    EXPECT_TRUE(sac.test_le(-1));
}

TEST_F(AtomicCounterTest, TestLessEqualWithCount) {
    atomic_counter< uint64_t > uac{2};
    EXPECT_FALSE(uac.test_le_with_count(1).first);
    const auto uresult1{uac.test_le_with_count(2)};
    EXPECT_TRUE(uresult1.first);
    EXPECT_EQ(uresult1.second, static_cast< uint64_t >(2));
    const auto uresult2{uac.test_le_with_count(3)};
    EXPECT_TRUE(uresult2.first);
    EXPECT_EQ(uresult2.second, static_cast< uint64_t >(2));

    atomic_counter< int64_t > sac{-2};
    EXPECT_FALSE(sac.test_le_with_count(-3).first);
    const auto sresult1{sac.test_le_with_count(-2)};
    EXPECT_TRUE(sresult1.first);
    EXPECT_EQ(sresult1.second, static_cast< int64_t >(-2));
    const auto sresult2{sac.test_le_with_count(-1)};
    EXPECT_TRUE(sresult2.first);
    EXPECT_EQ(sresult2.second, static_cast< int64_t >(-2));
}

TEST_F(AtomicCounterTest, TestGreaterEqual) {
    const atomic_counter< uint64_t > uac{2};
    EXPECT_FALSE(uac.test_ge(3));
    EXPECT_TRUE(uac.test_ge(2));
    EXPECT_TRUE(uac.test_ge(1));

    const atomic_counter< int64_t > sac{-2};
    EXPECT_FALSE(sac.test_ge(-1));
    EXPECT_TRUE(sac.test_ge(-2));
    EXPECT_TRUE(sac.test_ge(-3));
}

TEST_F(AtomicCounterTest, TestGreaterEqualWithCount) {
    atomic_counter< uint64_t > uac{2};
    EXPECT_FALSE(uac.test_ge_with_count(3).first);
    const auto uresult1{uac.test_ge_with_count(2)};
    EXPECT_TRUE(uresult1.first);
    EXPECT_EQ(uresult1.second, static_cast< uint64_t >(2));
    const auto uresult2{uac.test_ge_with_count(1)};
    EXPECT_TRUE(uresult2.first);
    EXPECT_EQ(uresult2.second, static_cast< uint64_t >(2));

    atomic_counter< int64_t > sac{-2};
    EXPECT_FALSE(sac.test_ge_with_count(-1).first);
    const auto sresult1{sac.test_ge_with_count(-2)};
    EXPECT_TRUE(sresult1.first);
    EXPECT_EQ(sresult1.second, static_cast< int64_t >(-2));
    const auto sresult2{sac.test_ge_with_count(-3)};
    EXPECT_TRUE(sresult2.first);
    EXPECT_EQ(sresult2.second, static_cast< int64_t >(-2));
}

SISL_OPTIONS_ENABLE(logging, test_atomic_counter)

SISL_OPTION_GROUP(test_atomic_counter,
                  (num_threads, "", "num_threads", "number of threads",
                   ::cxxopts::value< size_t >()->default_value("8"), "number"))

int main(int argc, char* argv[]) {
    int parsed_argc{argc};
    ::testing::InitGoogleTest(&parsed_argc, argv);
    testing::FLAGS_gtest_death_test_style = "threadsafe";
    SISL_OPTIONS_LOAD(parsed_argc, argv, logging, test_atomic_counter);
    sisl::logging::SetLogger("test_atomic_counter");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%n] [%t] %v");

    g_num_threads = SISL_OPTIONS["num_threads"].as< size_t >();

    const auto ret{RUN_ALL_TESTS()};
    return ret;
}
