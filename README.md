# NanoCU

Nano Concurrency Utils.  
Provides some simple concurrency and lock-free tools. 

一个临时 repo.


## Queue

Header-Only，位于 `include/` 下。

Namespace `NanoCU::MPMCQueue` 中，  
`ConcurrentQueue` 并发队列，非 lockfree 保证。  
`BatchedConcurrentQueue` 块状链表优化的并发队列，非 lockfree 保证。  
`LockFreeQueue` LockFree 队列，保证 Sequential Consistency，不保证 Linearizability.  
`BatchedLockFreeQueue` 块状链表优化的 LockFree 队列，保证 Sequential Consistency，不保证 Linearizability.  


### Bench

测试和 bench 文件位于 `test/` 下。
如 `test/lockfree_mpmc_queue/more_bench_test.cpp`.

```bash
cmake -DCMAKE_BUILD_TYPE=Release -S . -B ./.build_release
cmake --build ./.build_release

ctest -V --test-dir ./.build_release -R MPMCQueueBenchTest
ctest -V --test-dir ./.build_release -R LockFreeMPMCQueueBenchTest
ctest -V --test-dir ./.build_release -R LockFreeMPMCQueueMoreBenchTest

# 或执行
# `./.build_release/test/lockfree_mpmc_queue/lockfree_mpmc_queue_more_bench_test`
```

与 moodycamel 对比，在 push/pop 混合操作时性能相近。

```text
[==========] Running 2 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 2 tests from LockFreeMPMCQueueMoreBenchTest
[ RUN      ] LockFreeMPMCQueueMoreBenchTest.Basic
===== Start bench Batched LockFree MPMCQueue
===== End bench Batched LockFree MPMCQueue 792ms

===== Start bench moodycamel MPMCQueue
===== End bench moodycamel MPMCQueue 836ms

[       OK ] LockFreeMPMCQueueMoreBenchTest.Basic (8147 ms)
[ RUN      ] LockFreeMPMCQueueMoreBenchTest.Phased
===== Start bench Batched LockFree MPMCQueue
===== End bench Batched LockFree MPMCQueue 1170ms

===== Start bench moodycamel MPMCQueue
===== End bench moodycamel MPMCQueue 678ms

[       OK ] LockFreeMPMCQueueMoreBenchTest.Phased (9248 ms)
[----------] 2 tests from LockFreeMPMCQueueMoreBenchTest (17395 ms total)

[----------] Global test environment tear-down
[==========] 2 tests from 1 test suite ran. (17395 ms total)
[  PASSED  ] 2 tests.
```
