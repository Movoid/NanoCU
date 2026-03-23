#include <gtest/gtest.h>

#include <barrier>

#include "mpmc_queue/mpmc_queue.h"

TEST(MPMCQueueConcurrentTest, Basic) {
  constexpr std::size_t THREAD_CNT{10};
  constexpr std::size_t VAL_SCALE{10000000ul};

  std::vector<int> valtag(VAL_SCALE, 0);

  constexpr std::size_t PUSH_THREAD_CNT{THREAD_CNT / 2};
  constexpr std::size_t POP_THREAD_CNT{THREAD_CNT - PUSH_THREAD_CNT};

  std::vector<std::thread> workers(THREAD_CNT);
  std::barrier b{THREAD_CNT};

  NanoCU::MPMCQueue::LockFreeQueue<int, THREAD_CNT> q{};

  for (std::size_t i = 0; i < PUSH_THREAD_CNT; i++) {
    workers[i] = std::thread{[&q, &b, VAL_SCALE, PUSH_THREAD_CNT, i]() {
      b.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + PUSH_THREAD_CNT - 1) / PUSH_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (std::size_t val = beg; val < end; val++) {
        q.push(val);
      }
    }};
  }

  for (std::size_t i = 0; i < POP_THREAD_CNT; i++) {
    workers[PUSH_THREAD_CNT + i] = std::thread{[&valtag, &q, &b, VAL_SCALE, POP_THREAD_CNT, i]() {
      b.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + PUSH_THREAD_CNT - 1) / PUSH_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (std::size_t val = beg; val < end;) {
        auto res{q.try_pop()};
        if (res.has_value()) {
          val++;
          valtag[res.value()] = 1;
        }
      }
    }};
  }

  for (auto& worker : workers) {
    worker.join();
  }

  bool passed{true};
  int checksum{};
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
