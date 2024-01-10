#include "sisl/auth_manager/LRUCache.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sisl/options/options.h>
#include <future>

SISL_OPTIONS_ENABLE(logging)

namespace sisl::testing {

using namespace ::testing;

TEST(LRUTest, basic) {
    auto lru = LRUCache< int, int >(3);

    EXPECT_EQ(0, lru.size());
    EXPECT_FALSE(lru.exists(1));

    lru.put(0, 0);
    lru.put(1, 1);
    EXPECT_EQ(2, lru.size());
    EXPECT_TRUE(lru.exists(0));
    EXPECT_TRUE(lru.exists(1));

    lru.put(2, 2);

    // this will evict 0 from cache
    lru.put(3, 3);

    EXPECT_EQ(3, lru.size());

    EXPECT_FALSE(lru.exists(0));
    EXPECT_TRUE(lru.exists(1));
    EXPECT_TRUE(lru.exists(2));
    EXPECT_TRUE(lru.exists(3));

    // current elements in cache are 3, 2, 1
    // let's re-insert 1, this will move 1 to the head of cache
    lru.put(1, 1);

    // insert another new key, this will evict 2
    lru.put(4, 4);

    EXPECT_EQ(3, lru.size());
    EXPECT_FALSE(lru.exists(2));
    EXPECT_TRUE(lru.exists(1));
    EXPECT_TRUE(lru.exists(3));
    EXPECT_TRUE(lru.exists(4));
}

TEST(LRUTest, get) {
    auto lru = LRUCache< std::string, std::string >(3);

    lru.put("key1", "value1");
    EXPECT_EQ("value1", lru.get("key1"));
    auto v = lru.get("no-such-key");
    EXPECT_EQ(std::nullopt, v);

    // use variable as key, to test the perfect forwarding
    std::string key{"key2"};
    std::string value{"value2"};
    lru.put(key, value);
    ASSERT_TRUE(lru.get(key));
    EXPECT_EQ(value, lru.get(key));
}

} // namespace sisl::testing

int main(int argc, char* argv[]) {
    testing::InitGoogleMock(&argc, argv);
    SISL_OPTIONS_LOAD(argc, argv, logging)
    return RUN_ALL_TESTS();
}