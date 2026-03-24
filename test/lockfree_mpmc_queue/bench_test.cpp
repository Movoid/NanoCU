#include <gtest/gtest.h>

#include "lockfree_mpmc_queue/lockfree_mpmc_queue.h"
#include "test_helpers/mpmc_queue_test.h"

TEST(LockFreeMPMCQueueBenchTest, Basic) {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ull};

  bench(basic_concurrent_test<NanoCU::MPMCQueue::LockFreeQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>,
        "LockFree MPMCQueue");
  bench(basic_mutex_queue_test<THREAD_CNT, VAL_SCALE>, "MutexQueue");

  bench(basic_concurrent_test<NanoCU::MPMCQueue::LockFreeQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>,
        "LockFree MPMCQueue");
  bench(basic_mutex_queue_test<THREAD_CNT, VAL_SCALE>, "MutexQueue");
}

TEST(LockFreeMPMCQueueBenchTest, Phased) {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ull};

  bench(phased_concurrent_test<NanoCU::MPMCQueue::LockFreeQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>,
        "LockFree MPMCQueue");
  bench(phased_mutex_queue_test<THREAD_CNT, VAL_SCALE>, "MutexQueue");

  bench(phased_concurrent_test<NanoCU::MPMCQueue::LockFreeQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>,
        "LockFree MPMCQueue");
  bench(phased_mutex_queue_test<THREAD_CNT, VAL_SCALE>, "MutexQueue");
}