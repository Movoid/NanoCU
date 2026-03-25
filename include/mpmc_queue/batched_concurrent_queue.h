#pragma once

#include <atomic>

#include "hazard_ptr/hazard_ptr.h"

namespace NanoCU {

namespace MPMCQueue {

template <typename ValType, std::size_t BlockSize>
struct BatchedQueueNode {
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

  std::atomic<BatchedQueueNode*> to_next_{};
};

template <typename ValType, std::size_t WorkerCnt, std::size_t BlockSize = 32,
          typename ValAlloc = std::allocator<ValType>>
class BatchedConcurrentQueue
    : private EBOStorage<ValAlloc>,
      private EBOStorage<
          typename std::allocator_traits<ValAlloc>::template rebind_alloc<BatchedQueueNode<ValType, BlockSize>>> {
 private:
  using QueueNode_ = BatchedQueueNode<ValType, BlockSize>;
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
  BatchedConcurrentQueue(const BatchedConcurrentQueue&) = delete;
  BatchedConcurrentQueue(BatchedConcurrentQueue&&) = delete;
  auto operator=(const BatchedConcurrentQueue) -> BatchedConcurrentQueue& = delete;
  auto operator=(BatchedConcurrentQueue&&) -> BatchedConcurrentQueue& = delete;

  template <
      typename ValAlloc_ = ValAlloc,
      typename Requires_ = std::enable_if_t<!std::is_base_of_v<BatchedConcurrentQueue, std::remove_cvref_t<ValAlloc_>>>>
  BatchedConcurrentQueue(ValAlloc_&& val_alloc = ValAlloc{})
      : EBOStorage<ValAlloc>{std::forward<ValAlloc_>(val_alloc)},
        EBOStorage<NodeAlloc_>{static_cast<EBOStorage<ValAlloc>*>(this)->get()},
        hazptr_mgr_{static_cast<EBOStorage<ValAlloc>*>(this)->get(),
                    QueueNodeDeleter_{&(static_cast<EBOStorage<NodeAlloc_>*>(this)->get())}} {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};
    auto to_sentinel{std::allocator_traits<NodeAlloc_>::allocate(node_alloc, 1)};
    std::allocator_traits<NodeAlloc_>::construct(node_alloc, to_sentinel);
    to_head_.store(to_sentinel, std::memory_order_release);
    to_tail_.store(to_sentinel, std::memory_order_release);
  }

  BatchedConcurrentQueue() : BatchedConcurrentQueue{ValAlloc{}} {}

  ~BatchedConcurrentQueue() {
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

        auto new_start_slot_idx{old_start_slot_idx + 1};
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
         * 如果遇到 EMPTY 且将其成功设为 INVALID,
         * 则应该重启操作, 因为 queue 中后续可能有元素, queue 非空就不能返回 `std::nullopt`.
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
           * 创造 new node.
           */
          auto to_new_node{std::allocator_traits<NodeAlloc_>::allocate(node_alloc, 1)};
          std::allocator_traits<NodeAlloc_>::construct(node_alloc, to_new_node);

          /**
           * 当前 `to_old_tail` node 已不可用.
           * 尝试将 new node 挂到 tail,
           * 无论成功或失败, 都需要重启操作.
           */
          if (to_tail_.compare_exchange_strong(to_old_tail, to_new_node, std::memory_order_release,
                                               std::memory_order_relaxed)) {
            /**
             * 成功更新 `to_tail_`.
             * 此时取得 `to_old_tail` 的所有权.
             */
            to_old_tail->to_next_.store(to_new_node, std::memory_order_release);
          } else {
            /**
             * 更新 `to_tail_` 失败.
             * 有另一个 worker 抢先更新.
             */
            std::allocator_traits<NodeAlloc_>::destroy(node_alloc, to_new_node);
            std::allocator_traits<NodeAlloc_>::deallocate(node_alloc, to_new_node, 1);
          }
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
       *
       * WARNING: 如果 pop worker 一直设置 INVALID,
       * 则会陷入全局 livelock.
       */
      if (!to_old_tail->slot_states_[old_next_slot_idx].compare_exchange_strong(
              expected_slot_state, desired_slot_state, std::memory_order_release, std::memory_order_relaxed)) {
        assert((expected_slot_state == QueueNode_::SlotState_::INVALID) && "Unexpected.");
        push_val = std::move(to_old_tail->val_slots_[old_next_slot_idx].value());
        to_old_tail->val_slots_[old_next_slot_idx] = std::nullopt;
        continue;
      }

      hazptr_mgr_.unset_hazptr(1);
      return;
    }
  }
};

}  // namespace MPMCQueue

}  // namespace NanoCU
