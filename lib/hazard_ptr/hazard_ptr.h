#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "nano_utils/nano_utils.h"

namespace NanoCU {

namespace HazPtr {

template <std::size_t SlotCnt>
class HazPtrContext {
 private:
  std::array<CacheLineStorage<std::atomic<void*>>, SlotCnt> hazptrs_;

 public:
  HazPtrContext() : hazptrs_{make_array<CacheLineStorage<std::atomic<void*>>, SlotCnt>(nullptr)} {}

  HazPtrContext(const HazPtrContext&) = delete;
  HazPtrContext(HazPtrContext&&) = delete;
  auto operator=(const HazPtrContext&) -> HazPtrContext& = delete;
  auto operator=(HazPtrContext&&) -> HazPtrContext& = delete;

  auto contains(void* ptr) const -> bool {
    for (std::size_t i = 0; i < SlotCnt; i++) {
      if (hazptrs_[i].get().load(std::memory_order_acquire) == ptr) {
        return true;
      }
    }
    return false;
  }

  auto set_hazptr(std::size_t idx, void* ptr) -> bool {
    hazptrs_[idx].get().store(ptr, std::memory_order_seq_cst);
    return true;
  }

  auto unset_hazptr(std::size_t idx) -> bool {
    hazptrs_[idx].get().store(nullptr, std::memory_order_release);
    return true;
  }

  template <typename Container_,
            typename Requires_ = std::void_t<decltype(std::declval<Container_>().emplace(nullptr))>>
  void output(Container_* out) const {
    for (auto& hazptr : hazptrs_) {
      auto ptr{hazptr.get().load(std::memory_order_acquire)};
      if (ptr != nullptr) {
        out->emplace(ptr);
      }
    }
  }
};

template <typename ValType>
struct RetireNode {
  ValType* to_val_{};
  RetireNode* to_next_{};
};

template <typename ValType, typename ValAlloc = std::allocator<ValType>,
          typename ValDeleter = std::default_delete<ValType>>
class RetireContext
    : private EBOStorage<ValAlloc>,
      private EBOStorage<typename std::allocator_traits<ValAlloc>::template rebind_alloc<RetireNode<ValType>>>,
      private EBOStorage<ValDeleter> {
 private:
  using RetireNode_ = RetireNode<ValType>;

  RetireNode_* to_list_{};
  std::atomic<std::size_t> cnt_{};

  using NodeAlloc_ = typename std::allocator_traits<ValAlloc>::template rebind_alloc<RetireNode_>;

 public:
  RetireContext() = default;

  RetireContext(const RetireContext& obj) = delete;
  RetireContext(RetireContext&& obj) = delete;
  auto operator=(const RetireContext& obj) -> RetireContext& = delete;
  auto operator=(RetireContext&& obj) -> RetireContext& = delete;

  template <typename ValAlloc_, typename ValDeleter_>
  RetireContext(ValAlloc_&& alloc, ValDeleter_&& deleter)
      : EBOStorage<ValAlloc>{std::forward<ValAlloc_>(alloc)},
        EBOStorage<NodeAlloc_>{static_cast<EBOStorage<ValAlloc>*>(this)->get()},
        EBOStorage<ValDeleter>{std::forward<ValDeleter_>(deleter)} {}

  ~RetireContext() {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};
    auto& val_deleter{static_cast<EBOStorage<ValDeleter>*>(this)->get()};
    auto to_node{to_list_};
    while (to_node) {
      auto to_next{to_node->to_next_};
      val_deleter(to_node->to_val_);
      std::allocator_traits<NodeAlloc_>::destroy(node_alloc, to_node);
      std::allocator_traits<NodeAlloc_>::deallocate(node_alloc, to_node, 1);
      to_node = to_next;
    }
  }

  void retire(ValType* to_val) {
    auto& node_alloc{*static_cast<EBOStorage<NodeAlloc_>*>(this)};
    auto to_new_node{std::allocator_traits<NodeAlloc_>::allocate(node_alloc, 1)};
    std::allocator_traits<NodeAlloc_>::construct(node_alloc, to_new_node, to_val, to_list_);
    to_list_ = to_new_node;
    cnt_.fetch_add(1, std::memory_order_relaxed);
  }

  auto get_cnt() const -> std::size_t { return cnt_.load(std::memory_order_relaxed); }

  template <typename Container_,
            typename Requires_ = std::void_t<decltype(std::declval<Container_>().contains(nullptr))>>
  void reclaim_no_hazard(Container_&& hazptrs) {
    auto& node_alloc{static_cast<EBOStorage<NodeAlloc_>*>(this)->get()};
    auto& val_deleter{static_cast<EBOStorage<ValDeleter>*>(this)->get()};
    auto to_node{to_list_};
    std::size_t unsafe_cnt{};
    to_list_ = nullptr;
    cnt_.store(0, std::memory_order_relaxed);
    while (to_node) {
      auto to_next{to_node->to_next_};
      if (!hazptrs.contains(to_node->to_val_)) {
        val_deleter(to_node->to_val_);
        std::allocator_traits<NodeAlloc_>::destroy(node_alloc, to_node);
        std::allocator_traits<NodeAlloc_>::deallocate(node_alloc, to_node, 1);
      } else {
        to_node->to_next_ = to_list_;
        to_list_ = to_node;
        unsafe_cnt++;
      }
      to_node = to_next;
    }
    cnt_.fetch_add(unsafe_cnt, std::memory_order_relaxed);
  }
};

template <typename ValType, std::size_t WorkerCnt, std::size_t SlotCnt, typename ValAlloc = std::allocator<ValType>,
          typename ValDeleter = std::default_delete<ValType>>
class HazPtrManager : private EBOStorage<ValAlloc>, private EBOStorage<ValDeleter> {
 private:
  using HazPtrContext_ = HazPtrContext<SlotCnt>;
  using RetireContext_ = RetireContext<ValType, ValAlloc, ValDeleter>;

  std::array<CacheLineStorage<HazPtrContext_>, WorkerCnt> hazptr_ctxs_;
  std::array<CacheLineStorage<RetireContext_>, WorkerCnt> retire_ctxs_;
  std::atomic<std::size_t> next_ctx_idx_{};

  /** Custom TLS specific to this object. */
  struct LocalEntry {
    HazPtrContext_* to_local_hazptr_ctx_{};
    RetireContext_* to_local_retire_ctx_{};
  };
  inline static std::atomic<std::size_t> next_mgr_idx_{};
  const std::size_t this_mgr_idx_{};
  thread_local inline static std::unordered_map<std::size_t, LocalEntry> tls_map_;

 public:
  auto get_context() -> std::optional<LocalEntry> {
    auto iter{tls_map_.find(this_mgr_idx_)};
    if (iter != tls_map_.end()) {
      return std::make_optional(iter->second);
    }
    auto cur_ctx_idx_{next_ctx_idx_.load(std::memory_order_relaxed)};

    do {
      if (cur_ctx_idx_ >= WorkerCnt) {
        assert(false && "Unexpected worker count.");
        return std::nullopt;
      }
    } while (!next_ctx_idx_.compare_exchange_weak(cur_ctx_idx_, cur_ctx_idx_ + 1, std::memory_order_release,
                                                  std::memory_order_relaxed));

    tls_map_[this_mgr_idx_] = {&hazptr_ctxs_[cur_ctx_idx_].get(), &retire_ctxs_[cur_ctx_idx_].get()};
    return tls_map_[this_mgr_idx_];
  }

  auto collect_all_hazptrs() {
    std::unordered_set<void*, std::hash<void*>, std::equal_to<void*>,
                       typename std::allocator_traits<ValAlloc>::template rebind_alloc<void*>>
        res{static_cast<EBOStorage<ValAlloc>*>(this)->get()};
    for (const auto& hazptr_ctx : hazptr_ctxs_) {
      hazptr_ctx.get().output(&res);
    }
    return res;
  }

 public:
  HazPtrManager(const HazPtrManager& obj) = delete;
  HazPtrManager(HazPtrManager&& obj) = delete;
  auto operator=(const HazPtrManager& obj) -> HazPtrManager& = delete;
  auto operator=(HazPtrManager&& obj) -> HazPtrManager& = delete;

  template <typename ValAlloc_ = ValAlloc, typename ValDeleter_ = ValDeleter,
            typename Requires_ = std::enable_if_t<!std::is_base_of_v<HazPtrManager, std::remove_cvref_t<ValAlloc_>>>>
  HazPtrManager(ValAlloc_&& val_alloc = ValAlloc{}, ValDeleter_&& val_deleter = ValDeleter{})
      : EBOStorage<ValAlloc>{std::forward<ValAlloc_>(val_alloc)},
        EBOStorage<ValDeleter>{std::forward<ValDeleter_>(val_deleter)},
        hazptr_ctxs_{},
        retire_ctxs_{make_array<CacheLineStorage<RetireContext_>, WorkerCnt>(
            static_cast<EBOStorage<ValAlloc>*>(this)->get(), static_cast<EBOStorage<ValDeleter>*>(this)->get())},
        this_mgr_idx_{next_mgr_idx_.fetch_add(1, std::memory_order_relaxed)} {}

  HazPtrManager() : HazPtrManager(ValAlloc{}, ValDeleter{}) {}

  auto global_max_hazptr_cnt() -> std::size_t { return SlotCnt * WorkerCnt; }

  auto local_retired_cnt() -> std::size_t {
    auto ctx{get_context()};
    if (!ctx.has_value()) {
      return 0;
    }
    auto to_retire_ctx{ctx.value().to_local_retire_ctx_};
    return to_retire_ctx->get_cnt();
  }

  auto set_hazptr(std::size_t idx, void* ptr) -> bool {
    auto ctx{get_context()};
    if (!ctx.has_value()) {
      return false;
    }
    auto to_hazptr_ctx{ctx.value().to_local_hazptr_ctx_};
    return to_hazptr_ctx->set_hazptr(idx, ptr);
  }

  auto unset_hazptr(std::size_t idx) -> bool {
    auto ctx{get_context()};
    if (!ctx.has_value()) {
      return false;
    }
    auto to_hazptr_ctx{ctx.value().to_local_hazptr_ctx_};
    return to_hazptr_ctx->unset_hazptr(idx);
  }

  void retire(ValType* ptr) {
    auto ctx{get_context()};
    if (!ctx.has_value()) {
      return;
    }
    auto to_retire_ctx{ctx.value().to_local_retire_ctx_};
    return to_retire_ctx->retire(ptr);
  }

  auto check_local_hazptr(ValType* ptr) -> bool {
    auto ctx{get_context()};
    if (!ctx.has_value()) {
      return false;
    }
    auto to_hazptr_ctx{ctx.value().to_local_hazptr_ctx_};
    return to_hazptr_ctx->contains(static_cast<void*>(ptr));
  }

  void reclaim_local() {
    auto ctx{get_context()};
    if (!ctx.has_value()) {
      return;
    }
    auto to_retire_ctx{ctx.value().to_local_retire_ctx_};
    to_retire_ctx->reclaim_no_hazard(collect_all_hazptrs());
  }
};

}  // namespace HazPtr

}  // namespace NanoCU
