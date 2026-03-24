#include <gtest/gtest.h>

#include "mpmc_queue/batched_mpmc_queue.h"
#include "mpmc_queue/mpmc_queue.h"
#include "test_helpers/mpmc_queue_test.h"

TEST(MPMCQueueBenchTest, Basic) {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ull};

  bench(basic_concurrent_test<NanoCU::MPMCQueue::ConcurrentQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>,
        "MPMCQueue");
  bench(
      basic_concurrent_test<NanoCU::MPMCQueue::BatchedConcurrentQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>,
      "Batched MPMCQueue");
  bench(basic_mutex_queue_test<THREAD_CNT, VAL_SCALE>, "MutexQueue");
}

TEST(MPMCQueueBenchTest, Phased) {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ull};

  bench(phased_concurrent_test<NanoCU::MPMCQueue::ConcurrentQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>,
        "MPMCQueue");
  bench(
      phased_concurrent_test<NanoCU::MPMCQueue::BatchedConcurrentQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>,
      "Batched MPMCQueue");
  bench(phased_mutex_queue_test<THREAD_CNT, VAL_SCALE>, "MutexQueue");
}