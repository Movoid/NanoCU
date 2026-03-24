#pragma once

#include <gtest/gtest.h>

#include <barrier>
#include <chrono>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

template <typename QueueType, std::size_t ThreadCnt, std::size_t ValScale>
void basic_concurrent_test() {
  constexpr std::size_t THREAD_CNT{ThreadCnt};
  constexpr std::size_t VAL_SCALE{ValScale};
  std::vector<std::size_t> valtag(VAL_SCALE, 0);

  QueueType queue{};

  std::barrier b{THREAD_CNT};
  std::vector<std::thread> workers(THREAD_CNT);

  std::size_t PUSH_THREAD_CNT{THREAD_CNT / 2};
  std::size_t POP_THREAD_CNT{THREAD_CNT - PUSH_THREAD_CNT};

  for (std::size_t i = 0; i < PUSH_THREAD_CNT; i++) {
    workers[i] = std::thread{[&queue, &b, VAL_SCALE, PUSH_THREAD_CNT, i]() {
      b.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + PUSH_THREAD_CNT - 1) / PUSH_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (std::size_t val = beg; val < end; val++) {
        queue.push(val);
      }
    }};
  }

  for (std::size_t i = 0; i < POP_THREAD_CNT; i++) {
    workers[PUSH_THREAD_CNT + i] = std::thread{[&valtag, &queue, &b, VAL_SCALE, POP_THREAD_CNT, i]() {
      b.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + POP_THREAD_CNT - 1) / POP_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (std::size_t val = beg; val < end;) {
        auto res{queue.try_pop()};
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
  std::size_t checksum{};
  for (std::size_t i = 0; i < VAL_SCALE; i++) {
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

template <typename QueueType, std::size_t ThreadCnt, std::size_t ValScale>
void phased_concurrent_test() {
  constexpr std::size_t THREAD_CNT{ThreadCnt};
  constexpr std::size_t VAL_SCALE{ValScale};
  std::vector<std::size_t> valtag(VAL_SCALE, 0);

  QueueType queue{};

  std::vector<std::thread> workers(THREAD_CNT);

  constexpr std::size_t PUSH_THREAD_CNT{THREAD_CNT / 2};
  constexpr std::size_t POP_THREAD_CNT{THREAD_CNT - PUSH_THREAD_CNT};
  std::barrier b_pusher{PUSH_THREAD_CNT};
  std::barrier b_poper{POP_THREAD_CNT};

  for (std::size_t i = 0; i < PUSH_THREAD_CNT; i++) {
    workers[i] = std::thread{[&queue, &b_pusher, VAL_SCALE, PUSH_THREAD_CNT, i]() {
      b_pusher.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + PUSH_THREAD_CNT - 1) / PUSH_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (std::size_t val = beg; val < end; val++) {
        queue.push(val);
      }
    }};
  }

  for (std::size_t i = 0; i < PUSH_THREAD_CNT; i++) {
    workers[i].join();
  }

  for (std::size_t i = 0; i < POP_THREAD_CNT; i++) {
    workers[PUSH_THREAD_CNT + i] = std::thread{[&valtag, &queue, &b_pusher, VAL_SCALE, POP_THREAD_CNT, i]() {
      b_pusher.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + POP_THREAD_CNT - 1) / POP_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (std::size_t val = beg; val < end;) {
        auto res{queue.try_pop()};
        if (res.has_value()) {
          val++;
          valtag[res.value()] = 1;
        }
      }
    }};
  }

  for (std::size_t i = 0; i < POP_THREAD_CNT; i++) {
    workers[PUSH_THREAD_CNT + i].join();
  }

  bool passed{true};
  std::size_t checksum{};
  for (std::size_t i = 0; i < VAL_SCALE; i++) {
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

template <std::size_t ThreadCnt, std::size_t ValScale>
void basic_mutex_queue_test() {
  constexpr std::size_t THREAD_CNT{ThreadCnt};
  constexpr std::size_t VAL_SCALE{ValScale};
  std::vector<std::size_t> valtag(VAL_SCALE, 0);
  std::queue<std::size_t> queue{};
  std::mutex m{};

  std::barrier b{THREAD_CNT};
  std::vector<std::thread> workers(THREAD_CNT);

  std::size_t PUSH_THREAD_CNT{THREAD_CNT / 2};
  std::size_t POP_THREAD_CNT{THREAD_CNT - PUSH_THREAD_CNT};

  for (std::size_t i = 0; i < PUSH_THREAD_CNT; i++) {
    workers[i] = std::thread{[&queue, &b, &m, VAL_SCALE, PUSH_THREAD_CNT, i]() {
      b.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + PUSH_THREAD_CNT - 1) / PUSH_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (std::size_t val = beg; val < end; val++) {
        std::lock_guard<std::mutex> lock{m};
        queue.push(val);
      }
    }};
  }

  for (std::size_t i = 0; i < POP_THREAD_CNT; i++) {
    workers[PUSH_THREAD_CNT + i] = std::thread{[&valtag, &queue, &b, &m, VAL_SCALE, POP_THREAD_CNT, i]() {
      b.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + POP_THREAD_CNT - 1) / POP_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (int val = beg; val < end;) {
        std::lock_guard<std::mutex> l{m};
        if (queue.empty()) {
          continue;
        }
        val++;
        valtag[queue.front()] = 1;
        queue.pop();
      }
    }};
  }

  for (auto& worker : workers) {
    worker.join();
  }

  bool passed{true};
  std::size_t checksum{};
  for (std::size_t i = 0; i < VAL_SCALE; i++) {
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

template <std::size_t ThreadCnt, std::size_t ValScale>
void phased_mutex_queue_test() {
  constexpr std::size_t THREAD_CNT{ThreadCnt};
  constexpr std::size_t VAL_SCALE{ValScale};
  std::vector<std::size_t> valtag(VAL_SCALE, 0);
  std::queue<std::size_t> queue{};
  std::mutex m{};

  std::vector<std::thread> workers(THREAD_CNT);

  constexpr std::size_t PUSH_THREAD_CNT{THREAD_CNT / 2};
  constexpr std::size_t POP_THREAD_CNT{THREAD_CNT - PUSH_THREAD_CNT};

  std::barrier b_pusher{PUSH_THREAD_CNT};
  std::barrier b_poper{POP_THREAD_CNT};

  for (std::size_t i = 0; i < PUSH_THREAD_CNT; i++) {
    workers[i] = std::thread{[&queue, &b_pusher, &m, VAL_SCALE, PUSH_THREAD_CNT, i]() {
      b_pusher.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + PUSH_THREAD_CNT - 1) / PUSH_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (std::size_t val = beg; val < end; val++) {
        std::lock_guard<std::mutex> lock{m};
        queue.push(val);
      }
    }};
  }

  for (std::size_t i = 0; i < PUSH_THREAD_CNT; i++) {
    workers[i].join();
  }

  for (std::size_t i = 0; i < POP_THREAD_CNT; i++) {
    workers[PUSH_THREAD_CNT + i] = std::thread{[&valtag, &queue, &b_poper, &m, VAL_SCALE, POP_THREAD_CNT, i]() {
      b_poper.arrive_and_wait();
      std::size_t blksz{(VAL_SCALE + POP_THREAD_CNT - 1) / POP_THREAD_CNT};
      std::size_t beg{blksz * i};
      std::size_t end{std::min(beg + blksz, VAL_SCALE)};
      for (int val = beg; val < end;) {
        std::lock_guard<std::mutex> l{m};
        if (queue.empty()) {
          continue;
        }
        val++;
        valtag[queue.front()] = 1;
        queue.pop();
      }
    }};
  }

  for (std::size_t i = 0; i < POP_THREAD_CNT; i++) {
    workers[PUSH_THREAD_CNT + i].join();
  }

  bool passed{true};
  std::size_t checksum{};
  for (std::size_t i = 0; i < VAL_SCALE; i++) {
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
  std::cout << "===== End bench " << name << ' ' << std::chrono::duration_cast<std::chrono::milliseconds>(end1 - beg1)
            << '\n'
            << std::endl;
}};