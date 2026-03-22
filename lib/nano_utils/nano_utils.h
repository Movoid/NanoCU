#pragma once

#include <array>
#include <new>
#include <type_traits>
#include <utility>

namespace NanoCU {

constexpr auto CACHELINE_SIZE{std::hardware_constructive_interference_size};

/**
 * 禁止用于多态用途.
 * 析构非 `virtual`, 避免增加空间.
 */
template <typename Type, typename Requires = void>
class EBOStorage {
 private:
  Type data_{};

 public:
  EBOStorage() = default;

  template <typename Type_,
            typename Requires_ = std::enable_if_t<!std::is_base_of_v<EBOStorage, std::remove_cvref_t<Type_>> &&
                                                  std::is_constructible_v<Type, Type_&&>>>
  explicit EBOStorage(Type_&& data) : data_{std::forward<Type_>(data)} {}

  template <typename Type_,
            typename Requires_ = std::enable_if_t<!std::is_base_of_v<EBOStorage, std::remove_cvref_t<Type_>> &&
                                                  std::is_assignable_v<Type, Type_&&>>>
  auto operator=(Type_&& data) -> EBOStorage& {
    data_ = std::forward<Type_>(data);
    return *this;
  }

  template <typename Type_, typename... Args_,
            typename Requires_ = std::enable_if_t<!std::is_base_of_v<EBOStorage, std::remove_cvref_t<Type_>> &&
                                                  std::is_constructible_v<Type, Type_&&, Args_&&...>>>
  EBOStorage(Type_&& arg, Args_&&... args) : data_{std::forward<Type_>(arg), std::forward<Args_>(args)...} {}

  operator Type&() { return data_; }
  operator const Type&() const { return data_; }

  auto get() const -> const Type& { return data_; }
  auto get() -> Type& { return data_; }
};

template <typename Type>
class EBOStorage<Type, std::enable_if_t<std::is_class_v<Type> && std::is_empty_v<Type>>> : public Type {
 private:
 public:
  EBOStorage() = default;

  template <typename Type_,
            typename Requires_ = std::enable_if_t<!std::is_base_of_v<EBOStorage, std::remove_cvref_t<Type_>> &&
                                                  std::is_constructible_v<Type, Type_&&>>>
  explicit EBOStorage(Type_&& data) : Type{std::forward<Type_>(data)} {}

  template <typename Type_,
            typename Requires_ = std::enable_if_t<!std::is_base_of_v<EBOStorage, std::remove_cvref_t<Type_>> &&
                                                  std::is_assignable_v<Type, Type_&&>>>
  auto operator=(Type_&& data) -> EBOStorage& {
    Type::operator=(std::forward<Type_>(data));
    return *this;
  }

  template <typename Type_, typename... Args_,
            typename Requires_ = std::enable_if_t<!std::is_base_of_v<EBOStorage, std::remove_cvref_t<Type_>> &&
                                                  std::is_constructible_v<Type, Type_&&, Args_&&...>>>
  EBOStorage(Type_&& arg, Args_&&... args) : Type{std::forward<Type_>(arg), std::forward<Args_>(args)...} {}

  operator Type&() { return *this; }
  operator const Type&() const { return *this; }

  auto get() const -> const Type& { return *this; }
  auto get() -> Type& { return *this; }
};

/**
 * 禁止用于多态用途。
 */
template <typename Type, typename Requires = void>
class alignas(CACHELINE_SIZE) CacheLineStorage : public EBOStorage<Type> {
 private:
 public:
  CacheLineStorage() = default;

  template <typename Type_,
            typename Requires_ = std::enable_if_t<!std::is_base_of_v<CacheLineStorage, std::remove_cvref_t<Type_>> &&
                                                  std::is_constructible_v<Type, Type_&&>>>
  CacheLineStorage(Type_&& data) : EBOStorage<Type>{std::forward<Type_>(data)} {}

  template <typename Type_,
            typename Requires_ = std::enable_if_t<!std::is_base_of_v<CacheLineStorage, std::remove_cvref_t<Type_>> &&
                                                  std::is_assignable_v<Type, Type_&&>>>
  auto operator=(Type_&& data) -> CacheLineStorage& {
    EBOStorage<Type>::operator=(std::forward<Type_>(data));
    return *this;
  }

  template <typename Type_, typename... Args_,
            typename Requires_ = std::enable_if_t<!std::is_base_of_v<CacheLineStorage, std::remove_cvref_t<Type_>> &&
                                                  std::is_constructible_v<Type, Type_&&, Args_&&...>>>
  CacheLineStorage(Type_&& arg, Args_&&... args)
      : EBOStorage<Type>{std::forward<Type_>(arg), std::forward<Args_>(args)...} {}

  operator Type&() { return *static_cast<EBOStorage<Type>*>(this); }
  operator const Type&() const { return *static_cast<const EBOStorage<Type>*>(this); }

  auto get() -> Type& { return *static_cast<EBOStorage<Type>*>(this); }
  auto get() const -> const Type& { return *static_cast<const EBOStorage<Type>*>(this); }
};

/**
 * 注意, 构造 std::array<ValType, N> 不要用 ValType 拷贝构造.
 * 问题在于 ValType 不支持拷贝/移动.
 */
template <typename ArrEleType, std::size_t Len, typename... Args, std::size_t... Output>
auto make_array_solver(std::integral_constant<std::size_t, Len>, std::index_sequence<Output...>,
                       std::type_identity<ArrEleType>, const Args&... args) -> std::array<ArrEleType, Len> {
  static_assert(Len == sizeof...(Output), "Unexpected.");
  return {((void)Output, ArrEleType{args...})...};
}

template <typename ArrEleType, std::size_t Len, typename... Args>
auto make_array(const Args&... args) -> std::array<ArrEleType, Len> {
  return make_array_solver(std::integral_constant<std::size_t, Len>{}, std::make_index_sequence<Len>{},
                           std::type_identity<ArrEleType>{}, args...);
}

}  // namespace NanoCU