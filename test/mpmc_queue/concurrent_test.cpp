#include <gtest/gtest.h>

#include "mpmc_queue/batched_mpmc_queue.h"
#include "mpmc_queue/mpmc_queue.h"
#include "test_helpers/mpmc_queue_test.h"

TEST(MPMCQueueConcurrentTest, Basic) {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ull};

  basic_concurrent_test<NanoCU::MPMCQueue::ConcurrentQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>();
}

TEST(MPMCQueueConcurrentTest, Phased) {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ull};

  phased_concurrent_test<NanoCU::MPMCQueue::ConcurrentQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>();
}

TEST(BatchedMPMCQueueConcurrentTest, Basic) {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ull};

  basic_concurrent_test<NanoCU::MPMCQueue::BatchedConcurrentQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>();
}

TEST(BatchedMPMCQueueConcurrentTest, Phased) {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ull};

  phased_concurrent_test<NanoCU::MPMCQueue::BatchedConcurrentQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>();
}
