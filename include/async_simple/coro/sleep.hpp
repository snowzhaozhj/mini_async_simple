#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_SLEEP_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_SLEEP_HPP_

#include "async_simple/coro/lazy.hpp"

namespace async_simple::coro {

template<typename Rep, typename Period>
Lazy<void> sleep(std::chrono::duration<Rep, Period> dur) {
  Executor *executor = co_await CurrentExecutor{};
  if (!executor) {
    std::this_thread::sleep_for(dur);
    co_return;
  }
  co_return co_await executor->ScheduleAfter(std::chrono::duration_cast<Executor::Duration>(dur));
}

} // namespace async_simple::coro

#endif // MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_SLEEP_HPP_
