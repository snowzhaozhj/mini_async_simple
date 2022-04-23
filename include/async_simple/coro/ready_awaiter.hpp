#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_READY_AWAITER_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_READY_AWAITER_HPP_

#include <coroutine>
#include <utility>

namespace async_simple::coro {

/// 可以借助ReadyAwaiter来co_await一个non-awaitable对象
template<typename T>
struct ReadyAwaiter {
  ReadyAwaiter(T value) : value_(std::move(value)) {}

  bool await_ready() const noexcept { return true; }
  void await_suspend(std::coroutine_handle<>) const noexcept {}
  T await_resume() noexcept { return std::move(value_); }

  T value_;
};

} // namespace async_simple::coro

#endif // MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_READY_AWAITER_HPP_
