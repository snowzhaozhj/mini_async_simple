#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_FUTURE_TRAIT_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_FUTURE_TRAIT_HPP_

#include "async_simple/base/try.hpp"

#include <type_traits>

namespace async_simple {

template<typename T>
class Future;

template<typename T>
struct IsFuture : std::false_type {
  using Inner = T;
};

template<>
struct IsFuture<void> : std::false_type {
  using Inner = Unit;
};

template<typename T>
struct IsFuture<Future<T>> : std::true_type {
  using Inner = T;
};

template<typename T, typename F>
struct TryCallableResult {
  using Result = std::invoke_result_t<F, Try<T> &&>;
  using ReturnsFuture = IsFuture<Result>;
  static constexpr bool is_try = true;
};

template<typename T, typename F>
struct ValueCallableResult {
  using Result = std::invoke_result_t<F, T &&>;
  using ReturnsFuture = IsFuture<Result>;
  static constexpr bool is_try = false;
};

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_FUTURE_TRAIT_HPP_
