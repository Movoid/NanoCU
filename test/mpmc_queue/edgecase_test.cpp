#include <gtest/gtest.h>

#include "mpmc_queue/batched_concurrent_queue.h"
// #include "mpmc_queue/concurrent_queue.h"
#include "test_helpers/mpmc_queue_test.h"

TEST(BatchedMPMCQueueConcurrentTest, EdgeCase) {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ull};

  bench(basic_concurrent_test<NanoCU::MPMCQueue::BatchedConcurrentQueue<std::size_t, THREAD_CNT, 1>, THREAD_CNT,
                              VAL_SCALE>,
        "Batched MPMCQueue");
  bench(phased_concurrent_test<NanoCU::MPMCQueue::BatchedConcurrentQueue<std::size_t, THREAD_CNT, 1>, THREAD_CNT,
                               VAL_SCALE>,
        "Batched MPMCQueue");
}
