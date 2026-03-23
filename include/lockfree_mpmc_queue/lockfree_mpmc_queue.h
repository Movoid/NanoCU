#pragma once

#include <atomic>

#include "hazard_ptr/hazard_ptr.h"

namespace NanoCU {

namespace MPMCQueue {

template <typename ValType>
struct QueueNode {
  std::optional<ValType> val_opt_;
  std::atomic<QueueNode*> to_next_{};
};

template <typename ValType, std::size_t WorkerCnt, typename ValAlloc = std::allocator<ValType>>
class LockFreeQueue
    : private EBOStorage<ValAlloc>,
      private EBOStorage<typename std::allocator_traits<ValAlloc>::template rebind_alloc<QueueNode<ValType>>> {
 private:
  using QueueNode_ = QueueNode<ValType>;
  using NodeAlloc_ = typename std::allocator_traits<ValAlloc>::template rebind_alloc<QueueNode_>;

  struct QueueNodeDeleter_ {
    NodeAlloc_* to_node_alloc_{};
    auto operator()(QueueNode_* to_node) const {
      std::allocator_traits<NodeAlloc_>::destroy(*to_node_alloc_, to_node);
      std::allocator_traits<NodeAlloc_>::deallocate(*to_node_alloc_, to_node, 1);
    }
  };

  std::atomic<QueueNode_*> to_sentinel_{};
  std::atomic<QueueNode_*> to_tail_{};
  HazPtr::HazPtrManager<QueueNode_, WorkerCnt, 3, NodeAlloc_, QueueNodeDeleter_> hazptr_mgr_;

 public:
  LockFreeQueue(const LockFreeQueue&) = delete;
  LockFreeQueue(LockFreeQueue&&) = delete;
  auto operator=(const LockFreeQueue) -> LockFreeQueue& = delete;
  auto operator=(LockFreeQueue&&) -> LockFreeQueue& = delete;

  template <typename ValAlloc_ = ValAlloc,
            typename Requires_ = std::enable_if_t<!std::is_base_of_v<LockFreeQueue, std::remove_cvref_t<ValAlloc_>>>>
  LockFreeQueue(ValAlloc_&& val_alloc = ValAlloc{})
      : EBOStorage<ValAlloc>{std::forward<ValAlloc_>(val_alloc)},
        EBOStorage<NodeAlloc_>{static_cast<EBOStorage<ValAlloc>*>(this)->get()},
        hazptr_mgr_{static_cast<EBOStorage<ValAlloc>*>(this)->get(),
                    QueueNodeDeleter_{&(static_cast<EBOStorage<NodeAlloc_>*>(this)->get())}} {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};
    auto to_sentinel{std::allocator_traits<NodeAlloc_>::allocate(node_alloc, 1)};
    std::allocator_traits<NodeAlloc_>::construct(node_alloc, to_sentinel);
    to_sentinel_.store(to_sentinel, std::memory_order_release);
    to_tail_.store(to_sentinel, std::memory_order_release);
  }

  LockFreeQueue() : LockFreeQueue{ValAlloc{}} {}

  ~LockFreeQueue() {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};
    auto to_node{to_sentinel_.load(std::memory_order_acquire)};
    while (to_node) {
      auto to_next{to_node->to_next_.load(std::memory_order_acquire)};
      std::allocator_traits<NodeAlloc_>::destroy(node_alloc, to_node);
      std::allocator_traits<NodeAlloc_>::deallocate(node_alloc, to_node, 1);
      to_node = to_next;
    }
  }

  auto try_pop() -> std::optional<ValType> {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};

    while (true) {
      auto to_sentinel{to_sentinel_.load(std::memory_order_acquire)};

      QueueNode_* tmp{};
      do {
        tmp = to_sentinel;
        hazptr_mgr_.set_hazptr(0, to_sentinel);
        to_sentinel = to_sentinel_.load(std::memory_order_seq_cst);
      } while (to_sentinel != tmp);
      QueueNode_* to_old_head{to_sentinel->to_next_.load(std::memory_order_acquire)};

      if (!to_old_head) {
        hazptr_mgr_.unset_hazptr(0);
        return std::nullopt;
      }

      /**
       * sentinel 是一种资格.
       * 将 sentinel 资格转移给 old head.
       * 如果转移成功, 这保证 `to_old_head` 的访问是安全的.
       */
      hazptr_mgr_.set_hazptr(1, to_old_head);

      /**
       * WARNING: 如果 `to_sentinel` 和当前 `to_tail_` 相同,
       * 则此处将发生 `to_sentinel_` 被推进到 `to_tail_` 之后.
       * 并在当前操作最后, `hazptr_mgr_.retire(to_sentinel);`
       *
       * 然而, 这虽然导致 `to_sentinel_` 指向了 `to_tail_` 之后的节点,
       * 整个算法仍是正确的.
       * 既然 `to_tail_` 之后存在节点, 则被 retire 的 `to_sentinel` 已经被 hazptr 保护.
       * 新的 push worker 到来时, 会完成这个未完成的工作,
       * 并将 `to_tail_` 推进到和 `to_sentinel_` 一样的位置.
       */

      if (!to_sentinel_.compare_exchange_strong(to_sentinel, to_old_head, std::memory_order_release,
                                                std::memory_order_relaxed)) {
        /**
         * 未能取得这个 old head 的所有权.
         * 有其他 worker 先一步将其获取.
         */
        hazptr_mgr_.unset_hazptr(1);
        hazptr_mgr_.unset_hazptr(0);
        continue;
      }

      /**
       * WARNING: 在此处, 另一个 worker 执行了 pop,
       * 并将当前 worker 的 `to_old_head` 进行 retire.
       * 需要仔细考虑生命周期问题.
       */

      /**
       * 成功转移 sentinel 资格到 old head.
       * 当前 worker 有 old head 的所有权, 其他 worker 将此 node 视为 sentinel.
       */
      ValType ret{std::move(to_old_head->val_opt_.value())};
      hazptr_mgr_.unset_hazptr(1);
      hazptr_mgr_.unset_hazptr(0);
      hazptr_mgr_.retire(to_sentinel);
      if (hazptr_mgr_.local_retired_cnt() >= 2 * hazptr_mgr_.global_max_hazptr_cnt()) {
        hazptr_mgr_.reclaim_local();
      }
      return std::make_optional(std::move(ret));
    }
  }

  template <typename ValType_, typename Requires_ = std::enable_if_t<std::is_constructible_v<ValType, ValType_&&>>>
  void push(ValType_&& val) {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};

    auto to_new_node{std::allocator_traits<NodeAlloc_>::allocate(node_alloc, 1)};
    std::allocator_traits<NodeAlloc_>::construct(node_alloc, to_new_node);
    to_new_node->val_opt_ = std::forward<ValType_>(val);
    while (true) {
      auto to_old_tail{to_tail_.load(std::memory_order_acquire)};
      QueueNode_* tmp{};
      do {
        tmp = to_old_tail;
        hazptr_mgr_.set_hazptr(2, to_old_tail);
        to_old_tail = to_tail_.load(std::memory_order_seq_cst);
      } while (to_old_tail != tmp);

      QueueNode_* expected_next_ptr{nullptr};
      /**
       * 尝试将 new node 加到末尾.
       * 此操作成功后, 新的元素就已经可见, 可以被 pop.
       */
      if (!to_old_tail->to_next_.compare_exchange_strong(expected_next_ptr, to_new_node, std::memory_order_release,
                                                         std::memory_order_relaxed)) {
        /**
         * Helping.
         * 顺便更新 `to_tail_`.
         * 若成功, 则当前 worker 帮助了另一个 worker 将其构造的 new node 更新为新 tail.
         * 若失败, 则另一个 worker 不需要帮助.
         */
        to_tail_.compare_exchange_strong(to_old_tail, expected_next_ptr, std::memory_order_release,
                                         std::memory_order_relaxed);
        hazptr_mgr_.unset_hazptr(2);
        continue;
      }

      /**
       * 成功将当前构造的 new node 加到末尾.
       */
      to_tail_.compare_exchange_strong(to_old_tail, to_new_node, std::memory_order_release, std::memory_order_relaxed);
      hazptr_mgr_.unset_hazptr(2);
      return;
    }
  }
};

}  // namespace MPMCQueue

}  // namespace NanoCU
