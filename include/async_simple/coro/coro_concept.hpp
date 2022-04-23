#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_CORO_CONCEPT_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_CORO_CONCEPT_HPP_

#include <coroutine>
#include <utility>

namespace async_simple::coro {

namespace detail {

template<typename T>
concept HasCoAwaitMethod = requires(T&& t) {
  t.CoAwait(nullptr);
};

template<typename T>
concept HasMemberCoAwaitOperator = requires(T&& t) {
  t.operator co_await();
};

template<typename T>
concept HasGlobalCoAwaitOperator = requires(T&& t) {
  operator co_await(t);
};

template<typename Awaitable>
requires HasMemberCoAwaitOperator<Awaitable>
auto GetAwaiter(Awaitable &&awaitable) {
  return std::forward<Awaitable>(awaitable).operator co_await();
}

template<typename Awaitable>
requires HasGlobalCoAwaitOperator<Awaitable>
auto GetAwaiter(Awaitable &&awaitable) {
  return operator co_await(std::forward<Awaitable>(awaitable));
}

template<typename Awaitable>
requires (!HasMemberCoAwaitOperator<Awaitable>
    && !HasGlobalCoAwaitOperator<Awaitable>)
auto GetAwaiter(Awaitable &&awaitable) {
  return std::forward<Awaitable>(awaitable);
}

} // namespace async_simple::coro::detail

// 实现是有问题的，因为await_suspend并不一定接受的就是std::coroutine_handle<void>
template<typename T>
concept Awaiter = requires(T &&t, std::coroutine_handle<> h) {
  { t.await_ready() } -> std::convertible_to<bool>;
  t.await_suspend(h);
  t.await_resume();
};

template<typename T>
concept Awaitable = requires(T &&t) {
  { detail::GetAwaiter(std::forward<T>(t)) } -> Awaiter;
};

template<typename T>
struct AwaitResult {};

template<Awaitable T>
struct AwaitResult<T> {
  using type = decltype(std::declval<T>().await_resume());
};

template<typename T>
using AwaitResult_t = typename AwaitResult<T>::type;

namespace detail {

static_assert(Awaiter<std::suspend_always>);
static_assert(Awaiter<std::suspend_never>);
static_assert(Awaitable<std::suspend_always>);
static_assert(Awaitable<std::suspend_never>);
static_assert(std::is_same_v<AwaitResult_t<std::suspend_always>, void>);

struct __A {
  std::suspend_always operator co_await() { return {}; }
};

struct __B {

};
std::suspend_always operator co_await(__B) { return {}; }

struct __C {
  bool await_ready() { return true; }
  void await_suspend(std::coroutine_handle<>) {}
  void await_resume() {}
};

static_assert(Awaitable<__A>);
static_assert(Awaitable<__B>);
static_assert(Awaitable<__C>);

} // namespace async_simple::coro::detail

} // namespace async_simple::coro

#endif // MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_CORO_CONCEPT_HPP_
