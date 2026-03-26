#pragma once

#include <atomic>

#include "hazard_ptr/hazard_ptr.h"

namespace NanoCU {

namespace MPMCQueue {

template <typename ValType, std::size_t BlockSize>
struct BatchedLockFreeQueueNode {
  enum class SlotState_ {
    EMPTY,
    COMMITTED,
    INVALID,
  };
  std::array<std::atomic<SlotState_>, BlockSize> slot_states_;  // metadata
  std::array<std::optional<ValType>, BlockSize> val_slots_;

  std::atomic<std::size_t> start_slot_idx_{};
  static constexpr auto IS_SEALED{std::size_t{1} << (sizeof(std::size_t) * 8 - 1)};
  std::atomic<std::size_t> next_slot_idx_{};  // is_sealed.

  std::atomic<std::ptrdiff_t> max_committed_slot_idx_{-1};
  std::atomic<BatchedLockFreeQueueNode*> to_next_{};
};

template <typename ValType, std::size_t WorkerCnt, std::size_t BlockSize = 32,
          typename ValAlloc = std::allocator<ValType>>
class BatchedLockFreeQueue : private EBOStorage<ValAlloc>,
                             private EBOStorage<typename std::allocator_traits<ValAlloc>::template rebind_alloc<
                                 BatchedLockFreeQueueNode<ValType, BlockSize>>> {
 private:
  using QueueNode_ = BatchedLockFreeQueueNode<ValType, BlockSize>;
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
  HazPtr::HazPtrManager<QueueNode_, WorkerCnt, 2, NodeAlloc_, QueueNodeDeleter_> hazptr_mgr_;

 public:
  BatchedLockFreeQueue(const BatchedLockFreeQueue&) = delete;
  BatchedLockFreeQueue(BatchedLockFreeQueue&&) = delete;
  auto operator=(const BatchedLockFreeQueue) -> BatchedLockFreeQueue& = delete;
  auto operator=(BatchedLockFreeQueue&&) -> BatchedLockFreeQueue& = delete;

  template <
      typename ValAlloc_ = ValAlloc,
      typename Requires_ = std::enable_if_t<!std::is_base_of_v<BatchedLockFreeQueue, std::remove_cvref_t<ValAlloc_>>>>
  BatchedLockFreeQueue(ValAlloc_&& val_alloc = ValAlloc{})
      : EBOStorage<ValAlloc>{std::forward<ValAlloc_>(val_alloc)},
        EBOStorage<NodeAlloc_>{static_cast<EBOStorage<ValAlloc>*>(this)->get()},
        hazptr_mgr_{static_cast<EBOStorage<ValAlloc>*>(this)->get(),
                    QueueNodeDeleter_{&(static_cast<EBOStorage<NodeAlloc_>*>(this)->get())}} {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};
    auto to_new_node{std::allocator_traits<NodeAlloc_>::allocate(node_alloc, 1)};
    std::allocator_traits<NodeAlloc_>::construct(node_alloc, to_new_node);
    to_head_.store(to_new_node, std::memory_order_release);
    to_tail_.store(to_new_node, std::memory_order_release);
  }

  BatchedLockFreeQueue() : BatchedLockFreeQueue{ValAlloc{}} {}

  ~BatchedLockFreeQueue() {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};
    auto to_node{to_head_.load(std::memory_order_acquire)};
    while (to_node) {
      auto to_next{to_node->to_next_.load(std::memory_order_acquire)};
      std::allocator_traits<NodeAlloc_>::destroy(node_alloc, to_node);
      std::allocator_traits<NodeAlloc_>::deallocate(node_alloc, to_node, 1);
      to_node = to_next;
    }
  }

  auto try_pop() -> std::optional<ValType> {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};

    auto try_reclaim{[&]() {
      if (hazptr_mgr_.local_retired_cnt() >= 2 * hazptr_mgr_.global_max_hazptr_cnt()) {
        hazptr_mgr_.reclaim_local();
      }
    }};

    while (true) {
      auto to_old_head{to_head_.load(std::memory_order_relaxed)};

      QueueNode_* tmp{};
      do {
        tmp = to_old_head;
        hazptr_mgr_.set_hazptr(0, to_old_head);
        to_old_head = to_head_.load(std::memory_order_seq_cst);
      } while (to_old_head != tmp);

      std::size_t old_start_slot_idx{ULLONG_MAX};

      bool need_restart{};
      while (true) {
        old_start_slot_idx = to_old_head->start_slot_idx_.load(std::memory_order_acquire);
        auto old_next_slot_idx_with_flag{to_old_head->next_slot_idx_.load(std::memory_order_relaxed)};
        auto old_next_slot_idx{old_next_slot_idx_with_flag & (~QueueNode_::IS_SEALED)};

        /**
         * 如果 pop 到了当前 Node 终点.
         * 如果当前 Node `to_next_ == nullptr` 则没有可 pop 的元素.
         * 此外, 如果 is_sealed, 则此 Node 被完全消耗, 移动 `to_head_`.
         */
        if (old_start_slot_idx == old_next_slot_idx) {
          auto to_next{to_old_head->to_next_.load(std::memory_order_acquire)};
          if (!to_next) {
            hazptr_mgr_.unset_hazptr(0);
            return std::nullopt;
          }

          if (old_next_slot_idx_with_flag & (QueueNode_::IS_SEALED)) {
            if (to_head_.compare_exchange_strong(to_old_head, to_next, std::memory_order_release,
                                                 std::memory_order_relaxed)) {
              hazptr_mgr_.retire(to_old_head);
              try_reclaim();
            }
          }

          need_restart = true;
          break;
        }

        /**
         * 在确定当前 slot 有元素时才能获取此 slot.
         * 否则将陷入 livelock 困境.
         *
         * 然而, 就算想要获得的 slot 上是 empty,
         * 如果队列后面有元素, 就不能返回 `std::nullopt`.
         *
         * WARNING: 不能因为当前想要获得的 slot 上是 empty 就直接返回 `std::nullopt`,
         * 如果当前 slot 上的 push 没有和当前 worker 同步,
         * 而 queue 后面的元素 push worker 与当前 worker 有同步,
         * 即使这个 slot 是 empty,
         * 然而 queue 后续的元素仍是可见的, queue 是可见的非空状态.
         *
         * 一种解法是,
         * 只在当前 slot 能确定是 queue 最后一个 slot 时,
         * 且同时是 empty 时,
         * 才能确保 queue 后续没有元素,
         * 且当前 slot 元素还未构造,
         * 此时直接返回 `std::nullopt`,
         * 使得这个唯一的末尾元素, 总能成功构造, 整个系统永远前进.
         *
         * NOTE: 然而上述解法不能解决问题,
         * 如果有 2 个 push worker, slot 0 的 push worker 被 pop worker 设为 invalid,
         * 在 slot 1 构造完成前, 原本 slot 0 的 push worker 重启并占据 slot 2,
         * 而 pop worker 重启操作并访问 slot 1, 因为 slot 2 的存在, 将 slot 1 又设为 invalid,
         * 仍陷入 livelock.
         */

        /**
         * 这里的根本问题在于 pop worker 难以判断 queue 的后面是否存在可见的元素.
         * pop worker 如果能确定当前位置后面存在 committed 元素,
         * 就允许直接设置当前 slot 为 invalid 然后向后寻找.
         * 否则, 直接返回 `std::nullopt`, 因为后续没有可见的元素.
         *
         * 给每个 node 维护 max committed slot idx,
         * 根据此判断即可.
         */

        auto new_start_slot_idx{old_start_slot_idx + 1};
        auto to_next{to_old_head->to_next_.load(std::memory_order_relaxed)};
        auto max_committed_slot_idx{to_old_head->max_committed_slot_idx_.load(std::memory_order_relaxed)};
        /**
         * 如果当前想要取得的 slot `old_next_slot_idx` 及其之后, 不存在可见的元素,
         * 则获取此位置没有意义.
         */
        if (max_committed_slot_idx < static_cast<std::ptrdiff_t>(old_start_slot_idx) && !to_next) {
          hazptr_mgr_.unset_hazptr(0);
          return std::nullopt;
        }

        if (!to_old_head->start_slot_idx_.compare_exchange_strong(
                old_start_slot_idx, new_start_slot_idx, std::memory_order_relaxed, std::memory_order_relaxed)) {
          continue;
        }

        break;
      }
      if (need_restart) {
        continue;
      }

      assert((old_start_slot_idx != ULLONG_MAX) && "Unexpected.");

      auto slot_state{to_old_head->slot_states_[old_start_slot_idx].load(std::memory_order_acquire)};

      if (slot_state == QueueNode_::SlotState_::COMMITTED) {
        auto ret{std::move(to_old_head->val_slots_[old_start_slot_idx].value())};
        hazptr_mgr_.unset_hazptr(0);
        return std::make_optional(std::move(ret));
      } else {
        assert((slot_state == QueueNode_::SlotState_::EMPTY) && "Unexpected.");
        auto expected_slot_state{QueueNode_::SlotState_::EMPTY};
        auto desired_slot_state{QueueNode_::SlotState_::INVALID};
        /**
         * WARNING: 不能在这里无限制地设置 INVALID,
         * 这将陷入全局 livelock.
         *
         * 然而也不能在这里修复,
         * 因为此处已经拿到了 `old_start_slot_idx`, 必须操作.
         * 则必须在拿到 `old_start_slot_idx` 前就检查.
         */
        if (to_old_head->slot_states_[old_start_slot_idx].compare_exchange_strong(
                expected_slot_state, desired_slot_state, std::memory_order_acquire, std::memory_order_acquire)) {
          continue;
        } else {
          assert((expected_slot_state == QueueNode_::SlotState_::COMMITTED) && "Unexpected.");
          auto ret{std::move(to_old_head->val_slots_[old_start_slot_idx].value())};
          hazptr_mgr_.unset_hazptr(0);
          return std::make_optional(std::move(ret));
        }
      }
    }
  }

  /**
   * Node 没有用完不允许 Seal.
   * 除非 Batch Push.
   */
  template <typename ValType_, typename Requires_ = std::enable_if_t<std::is_constructible_v<ValType, ValType_&&>>>
  void push(ValType_&& val) {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};
    ValType push_val{std::forward<ValType_>(val)};

    while (true) {
      auto to_old_tail{to_tail_.load(std::memory_order_relaxed)};

      QueueNode_* tmp{};
      do {
        tmp = to_old_tail;
        hazptr_mgr_.set_hazptr(1, to_old_tail);
        to_old_tail = to_tail_.load(std::memory_order_seq_cst);
      } while (to_old_tail != tmp);

      std::size_t old_next_slot_idx{ULLONG_MAX};

      bool need_restart{};
      while (true) {
        auto old_next_slot_idx_with_flag{to_old_tail->next_slot_idx_.load(std::memory_order_acquire)};
        if (old_next_slot_idx_with_flag & QueueNode_::IS_SEALED) {
          /**
           * 尝试更新 `to_old_tail->to_next_` 到 new node.
           * 如果失败, 则析构 new node 并继续尝试更新 `to_tail_`.
           * 如果成功, helping 更新 `to_tail_`.
           */
          auto to_next{to_old_tail->to_next_.load(std::memory_order::acquire)};
          if (!to_next) {
            auto to_new_node{std::allocator_traits<NodeAlloc_>::allocate(node_alloc, 1)};
            std::allocator_traits<NodeAlloc_>::construct(node_alloc, to_new_node);

            /**
             * WARNING: 必须保证 new node 上至少有一个 committed val,
             * 保证一个节点 `to_next_ != nullptr` 时, 一定表明后续存在 committed val.
             *
             * 注意设置各种 slot idx.
             */
            to_new_node->next_slot_idx_.store(1, std::memory_order_relaxed);
            to_new_node->max_committed_slot_idx_.store(0, std::memory_order_relaxed);
            to_new_node->val_slots_[0] = std::move(push_val);
            to_new_node->slot_states_[0].store(QueueNode_::SlotState_::COMMITTED, std::memory_order_release);

            if (to_old_tail->to_next_.compare_exchange_strong(to_next, to_new_node, std::memory_order_release,
                                                              std::memory_order_acquire)) {
              to_next = to_new_node;
              to_tail_.compare_exchange_strong(to_old_tail, to_next, std::memory_order_release,
                                               std::memory_order_relaxed);
              hazptr_mgr_.unset_hazptr(1);
              return;
            } else {
              push_val = std::move(to_new_node->val_slots_[0].value());
              to_new_node->val_slots_[0] = std::nullopt;
              std::allocator_traits<NodeAlloc_>::destroy(node_alloc, to_new_node);
              std::allocator_traits<NodeAlloc_>::deallocate(node_alloc, to_new_node, 1);
            }
          }

          to_tail_.compare_exchange_strong(to_old_tail, to_next, std::memory_order_release, std::memory_order_relaxed);
          need_restart = true;
          break;
        }

        old_next_slot_idx = old_next_slot_idx_with_flag & (~QueueNode_::IS_SEALED);
        auto new_next_slot_idx_with_flag{old_next_slot_idx + 1};
        if (new_next_slot_idx_with_flag == to_old_tail->val_slots_.size()) {
          new_next_slot_idx_with_flag |= QueueNode_::IS_SEALED;
        }

        /**
         * 尝试占有一个 slot.
         * 若失败, 则重新访问当前 node.
         */
        if (!to_old_tail->next_slot_idx_.compare_exchange_strong(old_next_slot_idx_with_flag,
                                                                 new_next_slot_idx_with_flag, std::memory_order_relaxed,
                                                                 std::memory_order_relaxed)) {
          continue;
        }

        /**
         * 拿到了 `old_next_slot_idx` .
         */
        break;
      }
      if (need_restart) {
        continue;
      }

      assert((old_next_slot_idx != ULLONG_MAX) && "Unexpected.");

      to_old_tail->val_slots_[old_next_slot_idx] = std::move(push_val);

      auto expected_slot_state{QueueNode_::SlotState_::EMPTY};
      auto desired_slot_state{QueueNode_::SlotState_::COMMITTED};
      /**
       * Release.
       * 如果没能及时 Commit, 则重启整个操作.
       */
      if (!to_old_tail->slot_states_[old_next_slot_idx].compare_exchange_strong(
              expected_slot_state, desired_slot_state, std::memory_order_release, std::memory_order_relaxed)) {
        assert((expected_slot_state == QueueNode_::SlotState_::INVALID) && "Unexpected.");
        push_val = std::move(to_old_tail->val_slots_[old_next_slot_idx].value());
        to_old_tail->val_slots_[old_next_slot_idx] = std::nullopt;
        continue;
      }

      /**
       * 成功设置 state 为 COMMITTED.
       * 尝试更新当前 node 的 max committed slot idx.
       *
       * WARNING: 即使一个 push worker 在此处卡死, 没有更新 max committed slot idx,
       * 未来其他的 push worker 也会推进 max committed slot idx.
       * 这不阻碍全局进展.
       *
       * 在这种设计下, release 的职责是由 slot state 和 max committed slot idx 上的操作共同承担的.
       */
      while (true) {
        auto old_max_committed_slot_idx{to_old_tail->max_committed_slot_idx_.load(std::memory_order_relaxed)};
        if (old_max_committed_slot_idx > static_cast<std::ptrdiff_t>(old_next_slot_idx)) {
          break;
        }
        /**
         * 如果 cmpxchg 失败,
         * 则重试, 并判断新的 max committed slot idx 是否已经超过当前 slot idx.
         */
        if (to_old_tail->max_committed_slot_idx_.compare_exchange_strong(
                old_max_committed_slot_idx, old_next_slot_idx, std::memory_order_relaxed, std::memory_order_relaxed)) {
          break;
        }
      }

      hazptr_mgr_.unset_hazptr(1);
      return;
    }
  }
};

}  // namespace MPMCQueue

}  // namespace NanoCU
