/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "sisl/fds/lru_map.hpp"

namespace {

TEST(LruMapTest, EmptyCacheGetReturnsDefaultValue) {
    sisl::LruMap< int, int > cache(2);
    EXPECT_EQ(cache.get(42), 0);
}

TEST(LruMapTest, EmptyCacheIteratorEqualsEnd) {
    sisl::LruMap< int, int > const cache(2);
    EXPECT_EQ(cache.begin(), cache.end());
}

TEST(LruMapTest, MissingKeyReturnsDefaultConstructedValue) {
    sisl::LruMap< int, std::shared_ptr< int > > cache(2);
    cache.set(1, std::make_shared< int >(10));

    EXPECT_EQ(cache.get(2), nullptr);
}

TEST(LruMapTest, ZeroCapacityImmediatelyEvictsInsertedEntry) {
    sisl::LruMap< int, int > cache(0);

    cache.set(1, 10);

    EXPECT_EQ(cache.get(1), 0);
    EXPECT_EQ(cache.begin(), cache.end());
}

TEST(LruMapTest, CapacityOfOneKeepsOnlyMostRecentEntry) {
    sisl::LruMap< int, int > cache(1);

    cache.set(1, 10);
    EXPECT_EQ(cache.get(1), 10);

    cache.set(2, 20);
    EXPECT_EQ(cache.get(1), 0);
    EXPECT_EQ(cache.get(2), 20);
}

TEST(LruMapTest, UpdatingExistingKeyDoesNotGrowCacheOrEvict) {
    sisl::LruMap< int, int > cache(2);

    cache.set(1, 10);
    cache.set(2, 20);
    // Key 1 already present -- this must update in place, not append, so nothing gets evicted.
    cache.set(1, 100);

    EXPECT_EQ(cache.get(1), 100);
    EXPECT_EQ(cache.get(2), 20);

    std::size_t count = 0;
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        ++count;
    }
    EXPECT_EQ(count, 2u);
}

TEST(LruMapTest, ReinsertingSameKeyRepeatedlyKeepsSingleEntry) {
    sisl::LruMap< int, int > cache(3);

    for (int i = 0; i < 5; ++i) {
        cache.set(1, i);
    }

    EXPECT_EQ(cache.get(1), 4);
    std::size_t count = 0;
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        ++count;
    }
    EXPECT_EQ(count, 1u);
}

// get() is a const lookup only -- it does NOT splice the entry to the front, so an entry that was
// merely read (not re-set) is still the least-recently-*set* item and can be evicted ahead of one
// that was set more recently, even though it was read after that.
TEST(LruMapTest, GetDoesNotRefreshRecency) {
    sisl::LruMap< int, int > cache(2);

    cache.set(1, 10);
    cache.set(2, 20);
    EXPECT_EQ(cache.get(1), 10); // touch key 1 via get() only

    cache.set(3, 30);           // eviction is driven by set() order, not get() order
    EXPECT_EQ(cache.get(1), 0); // key 1 still evicted despite the read above
    EXPECT_EQ(cache.get(2), 20);
    EXPECT_EQ(cache.get(3), 30);
}

TEST(LruMapTest, SettingFrontEntryAgainIsANoOpForOrdering) {
    sisl::LruMap< int, int > cache(2);

    cache.set(1, 10);
    cache.set(2, 20);  // key 2 is now at the front
    cache.set(2, 200); // re-set the entry already at the front

    auto it = cache.begin();
    ASSERT_NE(it, cache.end());
    EXPECT_EQ(it->first, 2);
    EXPECT_EQ(it->second, 200);
}

TEST(LruMapTest, EvictsInStrictLruOrderAcrossMultipleInsertions) {
    sisl::LruMap< int, int > cache(3);

    cache.set(1, 1);
    cache.set(2, 2);
    cache.set(3, 3);
    cache.set(4, 4); // evicts 1
    cache.set(5, 5); // evicts 2

    EXPECT_EQ(cache.get(1), 0);
    EXPECT_EQ(cache.get(2), 0);
    EXPECT_EQ(cache.get(3), 3);
    EXPECT_EQ(cache.get(4), 4);
    EXPECT_EQ(cache.get(5), 5);
}

TEST(LruMapTest, WorksWithStringKeys) {
    sisl::LruMap< std::string, int > cache(2);

    cache.set("alpha", 1);
    cache.set("beta", 2);
    cache.set("gamma", 3); // evicts "alpha"

    EXPECT_EQ(cache.get("alpha"), 0);
    EXPECT_EQ(cache.get("beta"), 2);
    EXPECT_EQ(cache.get("gamma"), 3);
}

TEST(LruMapTest, AccessesMostRecentlyUsedEntriesFirst) {
    sisl::LruMap< int, int > cache(2);

    cache.set(1, 10);
    cache.set(2, 20);

    EXPECT_EQ(cache.get(1), 10);
    EXPECT_EQ(cache.get(2), 20);

    cache.set(3, 30);
    EXPECT_EQ(cache.get(1), 0);
    EXPECT_EQ(cache.get(2), 20);
    EXPECT_EQ(cache.get(3), 30);
}

TEST(LruMapTest, UpdatesExistingKeyAndRefreshesRecency) {
    sisl::LruMap< int, std::string > cache(2);

    cache.set(1, "old");
    cache.set(2, "new");
    cache.set(1, "fresh");
    cache.set(3, "latest");

    EXPECT_EQ(cache.get(1), "fresh");
    EXPECT_EQ(cache.get(2), "");
    EXPECT_EQ(cache.get(3), "latest");
}

TEST(LruMapTest, IteratorVisitsMostRecentlyUsedFirst) {
    sisl::LruMap< int, int > cache(3);
    cache.set(1, 11);
    cache.set(2, 22);
    cache.set(3, 33);

    auto it = cache.begin();
    ASSERT_NE(it, cache.end());
    EXPECT_EQ(it->first, 3);
    EXPECT_EQ(it->second, 33);

    ++it;
    ASSERT_NE(it, cache.end());
    EXPECT_EQ(it->first, 2);
    EXPECT_EQ(it->second, 22);

    ++it;
    ASSERT_NE(it, cache.end());
    EXPECT_EQ(it->first, 1);
    EXPECT_EQ(it->second, 11);

    ++it;
    EXPECT_EQ(it, cache.end());
}

} // namespace

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
