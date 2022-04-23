#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_FUTURE_AWAITER_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_FUTURE_AWAITER_HPP_

#include "async_simple/sync/future.hpp"

#include <coroutine>

namespace async_simple::coro {

template<typename T>
class FutureAwaiter {
 public:
  explicit FutureAwaiter(Future<T> &&future) : future_(std::move(future)) {}
  FutureAwaiter(FutureAwaiter &&rhs) : future_(std::move(rhs.future_)) {}
  FutureAwaiter(FutureAwaiter &) = delete;

  bool await_ready() { return future_.HasResult(); }
  void await_suspend(std::coroutine_handle<> continuation) {
    future_.SetContinuation([continuation](Try<T> &&t) mutable {
      continuation.resume();
    });
  }
  T await_resume() { return std::move(future_.Value()); }
 private:
  Future<T> future_;
};

template<typename T>
requires IsFuture<std::decay_t<T>>::value
auto operator co_await(T &&future) {
  return FutureAwaiter(std::move(future));
}

} // namespace async_simple::coro

#endif // MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_FUTURE_AWAITER_HPP_
