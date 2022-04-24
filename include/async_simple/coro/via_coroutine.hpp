#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_VIA_COROUTINE_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_VIA_COROUTINE_HPP_

#include "async_simple/base/assert.hpp"
#include "async_simple/coro/coro_concept.hpp"
#include "async_simple/executor/executor.hpp"

#include <coroutine>

namespace async_simple::coro {

class ViaCoroutine : noncopyable {
 public:
  struct promise_type {
    struct FinalAwaiter {
      FinalAwaiter(Executor::Context ctx) : context(ctx) {}
      bool await_ready() const noexcept { return false; }

      template<typename PromiseType>
      auto await_suspend(std::coroutine_handle<PromiseType> h) noexcept {
        auto &promise = h.promise();
        if (promise.executor) {
          promise.executor->Checkin([&promise]() {
            promise.continuation.resume();
          }, context);
        } else {
          promise.continuation.resume();
        }
      }
      void await_resume() const noexcept {}

      Executor::Context context;
    };  // struct FinalAwaiter

    promise_type(Executor *ex) : executor(ex), context(Executor::kNullContext) {}

    ViaCoroutine get_return_object() noexcept {
      return {std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    void return_void() noexcept {}
    void unhandled_exception() const noexcept { ASSERT(false); }
    std::suspend_always initial_suspend() const noexcept { return {}; }
    FinalAwaiter final_suspend() noexcept { return {context}; }

    Executor *executor;
    Executor::Context context;
    std::coroutine_handle<> continuation;
  };  // struct promise_type

  ViaCoroutine(std::coroutine_handle<promise_type> coro) : coro_(coro) {}
  ~ViaCoroutine() {
    if (coro_) {
      coro_.destroy();
      coro_ = nullptr;
    }
  }

  ViaCoroutine(ViaCoroutine &&other) : coro_(std::exchange(other.coro_, nullptr)) {}

  static ViaCoroutine Create(Executor *executor) { co_return; }

  void Checkin() {
    auto &promise = coro_.promise();
    if (promise.executor) {
      promise.executor->Checkin([]() {}, promise.context);
    }
  }

  std::coroutine_handle<> GetWrappedContinuation(std::coroutine_handle<> continuation) {
    ASSERT(coro_);
    auto &promise = coro_.promise();
    if (promise.executor) {
      promise.context = promise.executor->Checkout();
    }
    promise.continuation = continuation;
    return coro_;
  }

 private:
  std::coroutine_handle<promise_type> coro_;
};  // class ViaCoroutine

template<typename Awaiter>
struct ViaAsyncAwaiter {
  template<typename Awaitable>
  ViaAsyncAwaiter(Executor *ex, Awaitable &&awaitable)
      : executor(ex),
        awaiter(detail::GetAwaiter(std::forward<Awaitable>(awaitable))),
        via_coroutine(ViaCoroutine::Create(ex)) {}

  using AwaitSuspendResultType =
  decltype(std::declval<Awaiter>().await_suspend(
      std::declval<std::coroutine_handle<>>()));

  bool await_ready() { return awaiter.await_ready(); }
  AwaitSuspendResultType await_suspend(std::coroutine_handle<> continuation) {
    if constexpr(std::is_same_v<AwaitSuspendResultType, bool>) {
      bool should_suspend = awaiter.await_suspend(
          via_coroutine.GetWrappedContinuation(continuation));
      if (!should_suspend) {
        via_coroutine.Checkin();
      }
      return should_suspend;
    } else {
      return awaiter.await_suspend(via_coroutine.GetWrappedContinuation(continuation));
    }
  }

  auto await_resume() { return awaiter.await_resume(); }

  Executor *executor;
  Awaiter awaiter;
  ViaCoroutine via_coroutine;
};  // struct ViaAsyncAwaiter

template<typename Awaitable>
requires (!detail::HasCoAwaitMethod<Awaitable>)
inline auto CoAwait(Executor *executor, Awaitable &&awaitable) {
  using AwaiterType = decltype(detail::GetAwaiter(std::forward<Awaitable>(awaitable)));
  return ViaAsyncAwaiter<std::decay_t<AwaiterType>>(executor, std::forward<Awaitable>(awaitable));
}

template<typename Awaitable>
requires detail::HasCoAwaitMethod<Awaitable>
inline auto CoAwait(Executor *executor, Awaitable &&awaitable) {
  return std::forward<Awaitable>(awaitable).CoAwait(executor);
}

} // namespace async_simple::coro

#endif // MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_VIA_COROUTINE_HPP_
