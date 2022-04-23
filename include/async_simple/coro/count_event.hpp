#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_COUNT_EVENT_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_COUNT_EVENT_HPP_

#include "async_simple/base/noncopyable.hpp"

#include <atomic>
#include <coroutine>
#include <utility>

namespace async_simple::coro {

class CountEvent : noncopyable {
 public:
  CountEvent(std::size_t count) : count_(count + 1) {}
  CountEvent(CountEvent &&other)
      : count_(other.count_.exchange(0, std::memory_order_relaxed)),
        awaiting_coroutine_(std::exchange(other.awaiting_coroutine_, nullptr)) {}

  void SetAwaitingCoroutine(std::coroutine_handle<> h) {
    awaiting_coroutine_ = h;
  }

  [[nodiscard]] std::coroutine_handle<> Down(std::size_t n = 1) {
    auto old_count = count_.fetch_sub(n, std::memory_order_acq_rel);
    if (old_count == 1) {
      return std::exchange(awaiting_coroutine_, nullptr);
    }
    return nullptr;
  }
  [[nodiscard]] std::size_t DownCount(std::size_t n = 1) {
    return count_.fetch_sub(n, std::memory_order_acq_rel);
  }

 private:
  std::atomic<std::size_t> count_;
  std::coroutine_handle<> awaiting_coroutine_;
};

} // namespace async_simple::coro

#endif // MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_COUNT_EVENT_HPP_
