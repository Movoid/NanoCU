#include <gtest/gtest.h>

#include "mpmc_queue/batched_concurrent_queue.h"
#include "mpmc_queue/concurrent_queue.h"

TEST(MPMCQueueNormalTest, Basic1) {
  constexpr std::size_t THREAD_CNT{1};
  constexpr std::size_t VAL_SCALE{1000000ull};

  std::vector<std::size_t> valtag(VAL_SCALE, 0);

  NanoCU::MPMCQueue::ConcurrentQueue<std::size_t, THREAD_CNT> q{};

  for (std::size_t i = 0; i < VAL_SCALE; i++) {
    q.push(i);
  }

  while (auto res{q.try_pop()}) {
    valtag[res.value()] = 1;
  }

  bool passed{true};
  std::size_t checksum{};
  for (std::size_t val = 0; val < VAL_SCALE; val++) {
    checksum += valtag[val];
    if (valtag[val] != 1) {
      passed = false;
      break;
    }
  }

  std::cout << (passed ? "passed" : "failed") << std::endl;
  std::cout << checksum << std::endl;
  EXPECT_EQ(passed, true);
}

TEST(MPMCQueueNormalTest, Basic2) {
  constexpr std::size_t VAL_SCALE{10000ull};
  constexpr std::size_t TEST_CYCLE{100};

  bool passed{true};
  std::size_t checksum{};

  NanoCU::MPMCQueue::ConcurrentQueue<std::size_t, 1> q{};
  for (std::size_t cycle = 0; cycle < TEST_CYCLE; cycle++) {
    std::vector<std::size_t> valtag(VAL_SCALE, 0);
    for (std::size_t i = 0; i < VAL_SCALE; i++) {
      q.push(i);
    }

    while (auto res{q.try_pop()}) {
      valtag[res.value()] = 1;
    }

    for (std::size_t val = 0; val < VAL_SCALE; val++) {
      checksum += valtag[val];
      if (valtag[val] != 1) {
        passed = false;
        break;
      }
    }

    if (!passed) {
      break;
    }
  }

  std::cout << (passed ? "passed" : "failed") << std::endl;
  std::cout << checksum << std::endl;
  EXPECT_EQ(passed, true);
}

TEST(MPMCQueueNormalTest, BatchedBasic1) {
  constexpr std::size_t VAL_SCALE{10000ul};
  constexpr std::size_t TEST_CYCLE{100};

  bool passed{true};
  std::size_t checksum{};

  NanoCU::MPMCQueue::BatchedConcurrentQueue<std::size_t, 1, 32> q{};
  for (std::size_t cycle = 0; cycle < TEST_CYCLE; cycle++) {
    std::vector<std::size_t> valtag(VAL_SCALE, 0);
    for (std::size_t i = 0; i < VAL_SCALE; i++) {
      q.push(i);
    }

    while (auto res{q.try_pop()}) {
      valtag[res.value()] = 1;
    }

    for (std::size_t val = 0; val < VAL_SCALE; val++) {
      checksum += valtag[val];
      if (valtag[val] != 1) {
        passed = false;
        break;
      }
    }

    if (!passed) {
      break;
    }
  }

  std::cout << (passed ? "passed" : "failed") << std::endl;
  std::cout << checksum << std::endl;
  EXPECT_EQ(passed, true);
}
