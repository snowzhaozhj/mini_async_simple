#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_DETACHED_COROUTINE_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_DETACHED_COROUTINE_HPP_

#include <coroutine>
#include <cstdio>
#include <exception>

namespace async_simple::coro {

struct DetachedCoroutine {
  struct promise_type {
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    DetachedCoroutine get_return_object() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() {
      try {
        std::rethrow_exception(std::current_exception());
      } catch (const std::exception &e) {
        fprintf(stderr, "find exception %s", e.what());
        fflush(stderr);
        std::rethrow_exception(std::current_exception());
      }
    }
  };
};

} // namespace async_simple::coro

#endif // MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_DETACHED_COROUTINE_HPP_
