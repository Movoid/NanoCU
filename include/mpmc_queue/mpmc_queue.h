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

  std::atomic<QueueNode_*> to_head_{};
  std::atomic<QueueNode_*> to_tail_{};
  HazPtr::HazPtrManager<QueueNode_, WorkerCnt, 1, NodeAlloc_, QueueNodeDeleter_> hazptr_mgr_;

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
    to_head_.store(to_sentinel, std::memory_order_relaxed);
    to_tail_.store(to_sentinel, std::memory_order_relaxed);
  }

  LockFreeQueue() : LockFreeQueue{ValAlloc{}} {}

  ~LockFreeQueue() {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};
    auto to_node{to_head_.load(std::memory_order_relaxed)};
    while (to_node) {
      auto to_next{to_node->to_next_.load(std::memory_order_relaxed)};
      std::allocator_traits<NodeAlloc_>::destroy(node_alloc, to_node);
      std::allocator_traits<NodeAlloc_>::deallocate(node_alloc, to_node, 1);
      to_node = to_next;
    }
  }

  auto try_pop() -> std::optional<ValType> {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};
    auto to_old_head{to_head_.load(std::memory_order_relaxed)};
    auto to_old_tail{to_tail_.load(std::memory_order_relaxed)};
    if (to_old_head == to_old_tail) {
      return std::nullopt;
    }

    QueueNode_* to_old_head_next{};
    do {
      QueueNode_* tmp{};
      do {
        tmp = to_old_head;
        hazptr_mgr_.set_hazptr(0, to_old_head);
        to_old_head = to_head_.load(std::memory_order_seq_cst);
      } while (to_old_head != tmp);
      to_old_head_next = to_old_head->to_next_.load(std::memory_order_acquire);  // acquire.
    } while (to_old_head_next && !to_head_.compare_exchange_weak(to_old_head, to_old_head_next,
                                                                 std::memory_order_acq_rel, std::memory_order_relaxed));
    hazptr_mgr_.unset_hazptr(0);

    /**
     * 当前 `to_old_head` 是 Sentinel Node?
     */
    if (!to_old_head_next) {
      return std::nullopt;
    }

    auto ret{std::move(to_old_head->val_opt_.value())};
    hazptr_mgr_.retire(to_old_head);
    if (hazptr_mgr_.local_retired_cnt() >= 2 * hazptr_mgr_.global_max_hazptr_cnt()) {
      hazptr_mgr_.reclaim_local();
    }
    return std::make_optional(std::move(ret));
  }

  template <typename ValType_, typename Requires_ = std::enable_if_t<std::is_constructible_v<ValType, ValType_&&>>>
  void push(ValType_&& val) {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};
    auto to_old_tail{to_tail_.load(std::memory_order_relaxed)};
    auto to_new_node{std::allocator_traits<NodeAlloc_>::allocate(node_alloc, 1)};
    std::allocator_traits<NodeAlloc_>::construct(node_alloc, to_new_node);
    while (!to_tail_.compare_exchange_weak(to_old_tail, to_new_node, std::memory_order_acq_rel,
                                           std::memory_order_relaxed)) {
      ;
    }
    to_old_tail->val_opt_ = std::forward<ValType_>(val);
    to_old_tail->to_next_.store(to_new_node, std::memory_order_release);  // release.
  }
};

}  // namespace MPMCQueue

}  // namespace NanoCU
