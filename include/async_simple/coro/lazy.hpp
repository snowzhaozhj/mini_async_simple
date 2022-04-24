#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_LAZY_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_LAZY_HPP_

#include "async_simple/base/macro.hpp"
#include "async_simple/base/try.hpp"
#include "async_simple/coro/via_coroutine.hpp"
#include "async_simple/coro/detached_coroutine.hpp"
#include "async_simple/coro/ready_awaiter.hpp"
#include "async_simple/executor/executor.hpp"

namespace async_simple::coro {

template<typename T>
class Lazy;

struct Yield {};

namespace detail {

template<typename LazyType, typename IAlloc, typename OAlloc, bool Para>
struct CollectAllAwaiter;

template<typename LazyType, typename IAlloc>
struct CollectAnyAwaiter;

class LazyPromiseBase {
 public:
  struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }
    template<typename PromiseType>
    auto await_suspend(std::coroutine_handle<PromiseType> h) noexcept {
      return h.promise().continuation_;
    }
    void await_resume() noexcept {}
  };  // struct FinalAwaiter

  struct YieldAwaiter {
    YieldAwaiter(Executor *ex) : executor(ex) {}
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) {
      executor->Schedule([handle]() mutable {
        handle.resume();
      });
    }
    void await_resume() noexcept {}

    Executor *executor;
  }; // struct YieldAwaiter

  LazyPromiseBase() : executor_(nullptr) {}

  std::suspend_always initial_suspend() noexcept { return {}; }
  FinalAwaiter final_suspend() noexcept { return {}; }

  template<typename Awaitable>
  auto await_transform(Awaitable &&awaitable) {
    return CoAwait(executor_, std::forward<Awaitable>(awaitable));
  }
  auto await_transform(CurrentExecutor) {
    return ReadyAwaiter<Executor *>(executor_);
  }
  auto await_transform(Yield) { return YieldAwaiter(executor_); }

  Executor *executor_;
  std::coroutine_handle<> continuation_;
};

template<typename T>
class LazyPromise : public LazyPromiseBase {
  enum class ResultType {
    kEmpty,
    kValue,
    kException,
  };

 public:
  LazyPromise() noexcept {};
  ~LazyPromise() noexcept {
    switch (result_type_) {
      case ResultType::kValue:
        value_.~T();
        break;
      case ResultType::kException:
        exception_.~exception_ptr();
        break;
      default:
        break;
    }
  }

  Lazy<T> get_return_object() noexcept;

  template<typename V>
  requires std::is_convertible_v<V &&, T>
  void return_value(V &&value) noexcept(std::is_nothrow_constructible_v<T, V &&>) {
    ::new(static_cast<void *>(std::addressof(value_))) T(std::forward<V>(value));
    result_type_ = ResultType::kValue;
  }
  void unhandled_exception() noexcept {
    ::new(static_cast<void *>(std::addressof(exception_))) std::exception_ptr(std::current_exception());
    result_type_ = ResultType::kException;
  }

  T &Result() &{
    if (result_type_ == ResultType::kException) UNLIKELY {
      std::rethrow_exception(exception_);
    }
    ASSERT(result_type_ == ResultType::kValue);
    return value_;
  }
  T &&Result() &&{
    if (result_type_ == ResultType::kException) UNLIKELY {
      std::rethrow_exception(exception_);
    }
    ASSERT(result_type_ == ResultType::kValue);
    return std::move(value_);
  }

  Try<T> TryResult() noexcept {
    if (result_type_ == ResultType::kException) UNLIKELY {
      return Try<T>(exception_);
    } else {
      ASSERT(result_type_ == ResultType::kValue);
      return Try<T>(std::move(value_));
    }
  }

 private:
  ResultType result_type_{ResultType::kEmpty};
  union {
    T value_;
    std::exception_ptr exception_;
  };
};

template<>
class LazyPromise<void> : public LazyPromiseBase {
 public:
  Lazy<void> get_return_object() noexcept;
  void return_void() noexcept {}
  void unhandled_exception() noexcept {
    exception_ = std::current_exception();
  }
  void Result() {
    if (exception_ != nullptr) UNLIKELY {
      std::rethrow_exception(exception_);
    }
  }
  Try<void> TryResult() noexcept { return {exception_}; }

  std::exception_ptr exception_;
};

template<typename T>
struct LazyAwaiterBase : noncopyable {
  using Handle = std::coroutine_handle<LazyPromise<T>>;

  LazyAwaiterBase(Handle coro) : handle(coro) {}
  ~LazyAwaiterBase() {
    if (handle) {
      handle.destroy();
      handle = nullptr;
    }
  }

  LazyAwaiterBase(LazyAwaiterBase &&other)
      : handle(std::exchange(other.handle, nullptr)) {}

  LazyAwaiterBase &operator=(LazyAwaiterBase &&other) {
    handle = std::exchange(other.handle, nullptr);
    return *this;
  }

  bool await_ready() const noexcept { return false; }

  template<typename T2 = T>
  requires std::is_void_v<T2>
  void AwaitResume() {
    handle.promise().Result();
    handle.destroy();
    handle = nullptr;
  }

  template<typename T2 = T>
  requires (!std::is_void_v<T2>)
  T AwaitResume() {
    auto r = std::move(handle.promise()).Result();
    handle.destroy();
    handle = nullptr;
    return r;
  }

  Try<T> AwaitResumeTry() noexcept {
    Try<T> ret = std::move(handle.promise().TryResult());
    handle.destroy();
    handle = nullptr;
    return ret;
  }

  Handle handle;
};

} // namespace async_simple::coro::detail

template<typename T>
class RescheduleLazy;

template<typename T = void>
class Lazy : noncopyable {
 public:
  using promise_type = detail::LazyPromise<T>;
  using Handle = std::coroutine_handle<promise_type>;
  using ValueType = T;

  struct AwaiterBase : public detail::LazyAwaiterBase<T> {
    using Base = detail::LazyAwaiterBase<T>;
    AwaiterBase(Handle coro) : Base(coro) {}

    FORCE_INLINE std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
      Base::handle.promise().continuation_ = continuation;
      return Base::handle;
    }
  };

  struct TryAwaiter : public AwaiterBase {
    TryAwaiter(Handle coro) : AwaiterBase(coro) {}
    FORCE_INLINE Try<T> await_resume() noexcept {
      return AwaiterBase::AwaitResumeTry();
    }
  };

  struct ValueAwaiter : public AwaiterBase {
    ValueAwaiter(Handle coro) : AwaiterBase(coro) {}
    FORCE_INLINE T await_resume() { return AwaiterBase::AwaitResume(); }
  };

  explicit Lazy(Handle coro) : coro_(coro) {}
  ~Lazy() {
    if (coro_) {
      coro_.destroy();
      coro_ = nullptr;
    }
  }

  Lazy(Lazy &&other) : coro_(std::exchange(other.coro_, nullptr)) {}

  RescheduleLazy<T> Via(Executor *executor) &&{
    LOGIC_ASSERT(coro_.operator bool(),
                 "Lazy do not have a coroutine_handle");
    coro_.promise().executor_ = executor;
    return RescheduleLazy<T>(std::exchange(coro_, nullptr));
  }

  Lazy<T> SetExecutor(Executor *executor) &&{
    LOGIC_ASSERT(coro_.operator bool(),
                 "Lazy do not have a coroutine");
    coro_.promise().executor_ = executor;
    return Lazy<T>(std::exchange(coro_, nullptr));
  }

  template<typename F>
  void Start(F &&callback) {
    auto launch_coro = [](Lazy lazy, std::decay_t<F> cb) -> DetachedCoroutine {
      cb(std::move(co_await lazy.CoAwaitTry()));
    };
    [[maybe_unused]] auto detached = launch_coro(std::move(*this), std::forward<F>(callback));
  }

  bool IsReady() const { return !coro_ || coro_.done(); }

  auto operator co_await() {
    return ValueAwaiter(std::exchange(coro_, nullptr));
  }

  auto CoAwait(Executor *executor) {
    coro_.promise().executor_ = executor;
    return ValueAwaiter(std::exchange(coro_, nullptr));
  }

  auto CoAwaitTry() {
    return TryAwaiter(std::exchange(coro_, nullptr));
  }

 private:
  friend class RescheduleLazy<T>;

  template<typename C>
  friend typename std::decay_t<C>::ValueType SyncAwait(C &&);

  template<typename LazyType, typename IAlloc, typename OAlloc, bool Para>
  friend struct detail::CollectAllAwaiter;

  template<typename LazyType, typename IAlloc>
  friend struct detail::CollectAnyAwaiter;

  Handle coro_;
};

template<typename T = void>
class RescheduleLazy : noncopyable {
 public:
  using ValueType = typename Lazy<T>::ValueType;
  using Handle = typename Lazy<T>::Handle;

  struct AwaiterBase : public detail::LazyAwaiterBase<T> {
    using Base = detail::LazyAwaiterBase<T>;
    AwaiterBase(Handle coro) : Base(coro) {}
    inline void await_suspend(std::coroutine_handle<> continuation) noexcept {
      auto &promise = Base::handle.promise();
      promise.continuation_ = continuation;
      promise.executor_->Schedule([h = Base::handle]() mutable {
        h.resume();
      });
    }
  };

  struct ValueAwaiter : public AwaiterBase {
    ValueAwaiter(Handle coro) : AwaiterBase(coro) {}
    FORCE_INLINE T await_resume() { return AwaiterBase::AwaitResume(); }
  };

  struct TryAwaiter : public AwaiterBase {
    TryAwaiter(Handle coro) : AwaiterBase(coro) {}
    FORCE_INLINE Try<T> await_resume() noexcept {
      return AwaiterBase::AwaitResumeTry();
    }
  };

  RescheduleLazy(RescheduleLazy &&other)
      : coro_(std::exchange(other.coro_, nullptr)) {}

  auto operator co_await() noexcept {
    return ValueAwaiter(std::exchange(coro_, nullptr));
  }

  auto CoAwaitTry() { return TryAwaiter(std::exchange(coro_, nullptr)); }

  template<typename F>
  void Start(F &&callback) {
    auto launch_coro = [](RescheduleLazy lazy, std::decay_t<F> cb) -> DetachedCoroutine {
      cb(std::move(co_await lazy.CoAwaitTry()));
    };
    [[maybe_unused]] auto detached = launch_coro(std::move(*this), std::forward<F>(callback));
  }

  void Detach() {
    Start([](auto &&t) {
      if (t.HasException()) {
        std::rethrow_exception(t.GetException());
      }
    });
  }

 private:
  RescheduleLazy(Handle coro) : coro_(coro) {}

  friend class Lazy<T>;

  template<typename C>
  friend typename std::decay_t<C>::ValueType SyncAwait(C &&);

  template<typename LazyType, typename IAlloc, typename OAlloc, bool Para>
  friend struct detail::CollectAllAwaiter;

  template<typename LazyType, typename IAlloc>
  friend struct detail::CollectAnyAwaiter;

  Handle coro_;
};

template<typename T>
inline Lazy<T> detail::LazyPromise<T>::get_return_object() noexcept {
  return Lazy<T>(Lazy<T>::Handle::from_promise(*this));
}

inline Lazy<void> detail::LazyPromise<void>::get_return_object() noexcept {
  return Lazy<void>(Lazy<void>::Handle::from_promise(*this));
}

template<typename LazyType>
inline auto SyncAwait(LazyType &&lazy) -> typename std::decay_t<LazyType>::ValueType {
  LOGIC_ASSERT(lazy.coro_.operator bool(),
               "Lazy do not have a coroutine handle");
  auto executor = lazy.coro_.promise().executor_;
  if (executor) {
    LOGIC_ASSERT(!executor->CurrentThreadInExecutor(),
                 "do not sync await in the same executor with Lazy");
  }

  using ValueType = typename std::decay_t<LazyType>::ValueType;
  Try<ValueType> value;
  std::binary_semaphore sem(0);
  std::move(std::forward<LazyType>(lazy))
      .Start([&sem, &value](Try<ValueType> result) {
        value = std::move(result);
        sem.release();
      });
  sem.acquire();
  return std::move(value).Value();
}

} // namespace async_simple::coro

#endif // MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_LAZY_HPP_
