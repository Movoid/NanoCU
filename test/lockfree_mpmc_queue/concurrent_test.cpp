#include <gtest/gtest.h>

#include "lockfree_mpmc_queue/lockfree_queue.h"
#include "test_helpers/mpmc_queue_test.h"

TEST(LockFreeMPMCQueueConcurrentTest, Basic) {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ull};

  bench(basic_concurrent_test<NanoCU::MPMCQueue::LockFreeQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>,
        "LockFree MPMCQueue");
}

TEST(LockFreeMPMCQueueConcurrentTest, Phased) {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ull};

  bench(phased_concurrent_test<NanoCU::MPMCQueue::LockFreeQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>,
        "LockFree MPMCQueue");
}
