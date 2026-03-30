#include <gtest/gtest.h>

#include <boost/lockfree/queue.hpp>
#include <optional>
#include <thread>

#include "concurrentqueue.h"
#include "lockfree_mpmc_queue/batched_lockfree_queue.h"
#include "lockfree_mpmc_queue/lockfree_queue.h"
#include "test_helpers/mpmc_queue_test.h"

template <typename T>
class MoodyCamelQueueAdapter {
 public:
  void push(const T& value) {
    while (!queue_.enqueue(value)) {
      std::this_thread::yield();
    }
  }

  std::optional<T> try_pop() {
    T value{};
    if (queue_.try_dequeue(value)) {
      return value;
    }
    return std::nullopt;
  }

 private:
  moodycamel::ConcurrentQueue<T> queue_{};
};

// template <typename T, std::size_t Capacity>
// class BoostLockfreeQueueAdapter {
//  public:
//   BoostLockfreeQueueAdapter() : queue_(Capacity) {}

//   void push(const T& value) {
//     while (!queue_.push(value)) {
//       std::this_thread::yield();
//     }
//   }

//   std::optional<T> try_pop() {
//     T value{};
//     if (queue_.pop(value)) {
//       return value;
//     }
//     return std::nullopt;
//   }

//  private:
//   boost::lockfree::queue<T> queue_;
// };

TEST(LockFreeMPMCQueueMoreBenchTest, Basic) {
  constexpr std::size_t THREAD_CNT{10};
  constexpr std::size_t VAL_SCALE{10000000ull};

  bench(basic_concurrent_test<NanoCU::MPMCQueue::BatchedLockFreeQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>,
        "Batched LockFree MPMCQueue");
  bench(basic_concurrent_test<MoodyCamelQueueAdapter<std::size_t>, THREAD_CNT, VAL_SCALE>, "moodycamel MPMCQueue");
  // bench(basic_concurrent_test<BoostLockfreeQueueAdapter<std::size_t, VAL_SCALE>, THREAD_CNT, VAL_SCALE>,
  //       "boost::lockfree::queue");
}

TEST(LockFreeMPMCQueueMoreBenchTest, Phased) {
  constexpr std::size_t THREAD_CNT{10};
  constexpr std::size_t VAL_SCALE{10000000ull};

  bench(phased_concurrent_test<NanoCU::MPMCQueue::BatchedLockFreeQueue<std::size_t, THREAD_CNT>, THREAD_CNT, VAL_SCALE>,
        "Batched LockFree MPMCQueue");
  bench(phased_concurrent_test<MoodyCamelQueueAdapter<std::size_t>, THREAD_CNT, VAL_SCALE>, "moodycamel MPMCQueue");
  // bench(phased_concurrent_test<BoostLockfreeQueueAdapter<std::size_t, VAL_SCALE>, THREAD_CNT, VAL_SCALE>,
  //       "boost::lockfree::queue");
}
