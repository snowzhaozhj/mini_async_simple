#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_LOCAL_STATE_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_LOCAL_STATE_HPP_

#include "async_simple/base/try.hpp"
#include "async_simple/executor/executor.hpp"

namespace async_simple {

/// Future/Promise的组件
/// LocalState仅被Future持有
/// LocalState在Future和Promise断开后依然是有效的
template<typename T>
class LocalState : noncopyable {
  using Continuation = std::function<void(Try<T> &&value)>;
 public:
  LocalState() : executor_(nullptr) {}
  LocalState(T &&v) : try_value_(std::forward<T>(v)), executor_(nullptr) {}
  LocalState(Try<T> &&t) : try_value_(std::move(t)), executor_(nullptr) {}

  ~LocalState() = default;

  LocalState(LocalState &&other)
      : try_value_(std::move(other.try_value_)),
        executor_(std::exchange(other.executor_, nullptr)) {}
  LocalState &operator=(LocalState &&other) {
    if (this != &other) {
      std::swap(try_value_, other.try_value_);
      std::swap(executor_, other.executor_);
    }
    return *this;
  }

  bool HasResult() const noexcept { return try_value_.Available(); }

  Try<T> &GetTry() noexcept { return try_value_; }
  const Try<T> &GetTry() const noexcept { return try_value_; }

  void SetExecutor(Executor *executor) { executor_ = executor; }
  Executor *GetExecutor() { return executor_; }

  bool CurrentThreadInExecutor() const {
    if (executor_) {
      return executor_->CurrentThreadInExecutor();
    }
    return false;
  }

  template<typename F>
  void SetContinuation(F &&f) {
    ASSERT(try_value_.Available());
    std::forward<F>(f)(std::move(try_value_));
  }

 private:
  Try<T> try_value_;
  Executor *executor_;
};

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_LOCAL_STATE_HPP_
