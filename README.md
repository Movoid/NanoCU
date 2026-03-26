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

测试和 bench 文件位于 test/ 下。

```bash
cmake -DCMAKE_BUILD_TYPE=Release -S . -B ./.build_release
cmake --build ./.build_release

ctest -V --test-dir ./.build_release -R MPMCQueueBenchTest
ctest -V --test-dir ./.build_release -R LockFreeMPMCQueueBenchTest

# 或直接启动可执行文件, 如
# `./.build_release/test/lockfree_mpmc_queue/lockfree_mpmc_queue_bench_test`

```
