#include <gtest/gtest.h>

#include "lockfree_mpmc_queue/batched_lockfree_queue.h"
// #include "lockfree_mpmc_queue/lockfree_queue.h"
#include "test_helpers/mpmc_queue_test.h"

TEST(BatchedLockFreeMPMCQueueConcurrentTest, EdgeCase) {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ull};

  bench(
      basic_concurrent_test<NanoCU::MPMCQueue::BatchedLockFreeQueue<std::size_t, THREAD_CNT, 1>, THREAD_CNT, VAL_SCALE>,
      "Batched LockFree MPMCQueue");

  bench(phased_concurrent_test<NanoCU::MPMCQueue::BatchedLockFreeQueue<std::size_t, THREAD_CNT, 1>, THREAD_CNT,
                               VAL_SCALE>,
        "Batched LockFree MPMCQueue");
}
