#include <gtest/gtest.h>

#include <barrier>
#include <chrono>
#include <queue>

#include "lockfree_mpmc_queue/lockfree_mpmc_queue.h"

void lockfree_mpmcqueue_concurrent_bench_test() {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ul};
  std::vector<int> valtag(VAL_SCALE, 0);

  NanoCU::MPMCQueue::LockFreeQueue<int, THREAD_CNT> queue{};

  std::barrier b{THREAD_CNT};
  std::vector<std::thread> js(THREAD_CNT);

  std::size_t PUSH_THREAD_CNT{THREAD_CNT / 2};
  std::size_t POP_THREAD_CNT{THREAD_CNT - PUSH_THREAD_CNT};

  for (int i = 0; i < PUSH_THREAD_CNT; i++) {
    js[i] = std::thread{[&queue, &b, VAL_SCALE, PUSH_THREAD_CNT, i]() {
      b.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + PUSH_THREAD_CNT - 1) / PUSH_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (int i = beg; i < end; i++) {
        queue.push(i);
      }
    }};
  }

  for (int i = 0; i < POP_THREAD_CNT; i++) {
    js[PUSH_THREAD_CNT + i] = std::thread{[&valtag, &queue, &b, VAL_SCALE, POP_THREAD_CNT, i]() {
      b.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + POP_THREAD_CNT - 1) / POP_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (int i = beg; i < end;) {
        std::optional<int> res{queue.try_pop()};
        if (res.has_value()) {
          i++;
          valtag[res.value()] = 1;
        }
      }
    }};
  }

  for (int i = 0; i < THREAD_CNT; i++) {
    js[i].join();
  }

  bool passed{true};
  int checksum{};
  for (int i = 0; i < VAL_SCALE; i++) {
    checksum += valtag[i];
    if (valtag[i] != 1) {
      passed = false;
      break;
    }
  }
  std::cout << (passed ? "passed" : "failed") << std::endl;
  std::cout << checksum << std::endl;
  EXPECT_EQ(passed, true);
}

void normal_mutex_queue_test() {
  constexpr std::size_t THREAD_CNT{20};
  constexpr std::size_t VAL_SCALE{10000000ul};
  std::vector<int> valtag(VAL_SCALE, 0);
  // LockFreeQueue<int> queue{};
  std::queue<int> queue{};
  std::mutex m{};

  std::barrier b{THREAD_CNT};
  std::vector<std::thread> js(THREAD_CNT);

  std::size_t PUSH_THREAD_CNT{THREAD_CNT / 2};
  std::size_t POP_THREAD_CNT{THREAD_CNT - PUSH_THREAD_CNT};

  for (int i = 0; i < PUSH_THREAD_CNT; i++) {
    js[i] = std::thread{[&queue, &b, &m, VAL_SCALE, PUSH_THREAD_CNT, i]() {
      b.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + PUSH_THREAD_CNT - 1) / PUSH_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (int i = beg; i < end; i++) {
        std::lock_guard<std::mutex> lock{m};
        queue.push(i);
      }
    }};
  }

  for (int i = 0; i < POP_THREAD_CNT; i++) {
    js[PUSH_THREAD_CNT + i] = std::thread{[&valtag, &queue, &b, &m, VAL_SCALE, POP_THREAD_CNT, i]() {
      b.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + POP_THREAD_CNT - 1) / POP_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (int i = beg; i < end;) {
        std::lock_guard<std::mutex> l{m};
        if (queue.empty()) {
          continue;
        }
        i++;
        valtag[queue.front()] = 1;
        queue.pop();
      }
    }};
  }

  for (int i = 0; i < THREAD_CNT; i++) {
    js[i].join();
  }

  bool passed{true};
  int checksum{};
  for (int i = 0; i < VAL_SCALE; i++) {
    checksum += valtag[i];
    if (valtag[i] != 1) {
      passed = false;
      break;
    }
  }
  std::cout << (passed ? "passed" : "failed") << std::endl;
  std::cout << checksum << std::endl;
  EXPECT_EQ(passed, true);
}

auto bench{[](void (*func)(), const char name[]) {
  std::cout << "===== Start bench " << name << '\n';
  auto beg1{std::chrono::high_resolution_clock::now()};
  func();
  auto end1{std::chrono::high_resolution_clock::now()};
  std::cout << "===== End bench " << end1 - beg1 << std::endl;
}};

TEST(LockfreeMPMCQueueBenchTest, WithLockQueue) {
  bench(lockfree_mpmcqueue_concurrent_bench_test, "LockfreeMPMCQueue");
  bench(normal_mutex_queue_test, "MutexQueue");
  bench(normal_mutex_queue_test, "MutexQueue");
  bench(lockfree_mpmcqueue_concurrent_bench_test, "LockfreeMPMCQueue");
}