#include <gtest/gtest.h>

#include "nano_utils/nano_utils.h"

TEST(CacheLineStorageTest, Basic) {
  NanoCU::CacheLineStorage<int> a{1};

  EXPECT_EQ(sizeof(a), NanoCU::CACHELINE_SIZE);
  EXPECT_EQ(alignof(decltype(a)), NanoCU::CACHELINE_SIZE);

  struct A {
    NanoCU::CacheLineStorage<int> a_{};
    int b_{};
  };

  EXPECT_EQ(sizeof(A), NanoCU::CACHELINE_SIZE * 2);
  EXPECT_EQ(alignof(A), NanoCU::CACHELINE_SIZE);
}
