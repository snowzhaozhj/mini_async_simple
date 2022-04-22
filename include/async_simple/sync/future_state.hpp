#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_FUTURE_STATE_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_FUTURE_STATE_HPP_

#include "async_simple/base/try.hpp"
#include "async_simple/base/move_wrapper.hpp"
#include "async_simple/executor/executor.hpp"

#include <atomic>
#include <functional>

namespace async_simple {

namespace detail {

enum class State : uint8_t {
  kStart = 0,                 ///< 初始状态
  kOnlyResult = 1 << 0,       ///< 调用了promise.SetValue后的状态
  kOnlyContinuation = 1 << 1, ///< 调用了future.ThenImpl后的状态
  kDone = 1 << 5,
};

constexpr State operator|(State lhs, State rhs) {
  return State((uint8_t) lhs | (uint8_t) rhs);
}

constexpr State operator&(State lhs, State rhs) {
  return State((uint8_t) lhs & (uint8_t) rhs);
}

} // namespace async_simple::detail

/// FutureState是Future和Promise之间的共享状态
/// FutureState是Future/Promise模型的关键组件，它保证了线程安全性，以及会在需要的时候调用executor
template<typename T>
class FutureState : noncopyable {
  using Continuation = std::function<void(Try<T> &&value)>;

  class ContinuationReference {
   public:
    ContinuationReference() = default;
    explicit ContinuationReference(FutureState *fs) : fs_(fs) {
      Attach();
    }
    ~ContinuationReference() { Detach(); }

    ContinuationReference(const ContinuationReference &other)
        : fs_(other.fs_) {
      Attach();
    }
    ContinuationReference(ContinuationReference &&other) noexcept: fs_(std::exchange(other.fs_, nullptr)) {}

    ContinuationReference &operator=(const ContinuationReference &) = delete;
    ContinuationReference &operator=(ContinuationReference &&) = delete;

    FutureState *GetFutureState() const noexcept { return fs_; }

   private:
    void Attach() {
      if (fs_) {
        fs_->AttachOne();
        fs_->RefContinuation();
      }
    }
    void Detach() {
      if (fs_) {
        fs_->DerefContinuation();
        fs_->DetachOne();
      }
    }

    FutureState *fs_{nullptr};
  };
 public:
  FutureState()
      : state_(detail::State::kStart),
        attached_(0),
        continuation_ref_(0),
        promise_ref_(0),
        force_scheduled(false),
        executor_(nullptr),
        context_(Executor::kNullContext) {}
  ~FutureState() {}

  [[nodiscard]] bool HasResult() const noexcept {
    constexpr auto allow = detail::State::kDone | detail::State::kOnlyResult;
    auto state = state_.load(std::memory_order_acquire);
    return (state & allow) != detail::State{};
  }

  bool HasContinuation() const noexcept {
    constexpr auto allow = detail::State::kDone | detail::State::kOnlyContinuation;
    auto state = state_.load(std::memory_order_acquire);
    return (state & allow) != detail::State{};
  }

  FORCE_INLINE void AttachOne() {
    attached_.fetch_add(1, std::memory_order_relaxed);
  }
  FORCE_INLINE void DetachOne() {
    auto old = attached_.fetch_sub(1, std::memory_order_relaxed);
    ASSERT(old >= 1u);
    if (old == 1) {
      delete this;
    }
  }
  FORCE_INLINE void AttachPromise() {
    promise_ref_.fetch_add(1, std::memory_order_relaxed);
    AttachOne();
  }
  FORCE_INLINE void DetachPromise() {
    auto old = promise_ref_.fetch_sub(1, std::memory_order_relaxed);
    ASSERT(old >= 1u);
    if (!HasResult() && old == 1) {
      try {
        throw std::runtime_error("Promise is broken");
      } catch (...) {
        SetResult(Try<T>(std::current_exception()));
      }
    }
    DetachOne();
  }

  Try<T> &GetTry() noexcept { return try_value_; }
  const Try<T> &GetTry() const noexcept { return try_value_; }

  void SetExecutor(Executor *executor) { executor_ = executor; }
  Executor *GetExecutor() { return executor_; }

  /// 让continuation回到原来的context上
  void Checkout() {
    if (executor_) {
      context_ = executor_->Checkout();
    }
  }

  void SetForceScheduled(bool force = true) {
    if (!executor_ && force) {  // 其实感觉这段逻辑不是很必要
      return;
    }
    force_scheduled = force;
  }

  void SetResult(Try<T> &&value) {
    LOGIC_ASSERT(!HasResult(), "FutureState already has a result");
    try_value_ = std::move(value);

    auto state = state_.load(std::memory_order_acquire);
    switch (state) {
      case detail::State::kStart:
        if (state_.compare_exchange_strong(state,
                                           detail::State::kOnlyResult,
                                           std::memory_order_release)) {
          return;
        }
        /// 状态已经转移
        ASSERT(state_.load(std::memory_order_relaxed) == detail::State::kOnlyContinuation);
        [[fallthrough]];
      case detail::State::kOnlyContinuation:
        if (state_.compare_exchange_strong(state,
                                           detail::State::kDone,
                                           std::memory_order_release)) {
          ScheduleContinuation(false);
          return;
        }
        [[fallthrough]];
      default:
        LOGIC_ASSERT(false, "State Transfer Error");
    }
  }

  template<typename F>
  void SetContinuation(F &&func) {
    LOGIC_ASSERT(!HasContinuation(), "FutureState already has a continuation");
    MoveWrapper<F> lambda_func(std::move(func));
    new(&continuation_) Continuation([lambda_func](Try<T> &&v) mutable {
      auto &f = lambda_func.Get();
      f(std::forward<Try<T>>(v));
    });

    auto state = state_.load(std::memory_order_acquire);
    switch (state) {
      case detail::State::kStart:
        if (state_.compare_exchange_strong(state,
                                           detail::State::kOnlyContinuation)) {
          return;
        }
        // 状态已经改变
        ASSERT(state_.load(std::memory_order_relaxed) == detail::State::kOnlyResult);
        [[fallthrough]];
      case detail::State::kOnlyResult:
        if (state_.compare_exchange_strong(state,
                                           detail::State::kDone,
                                           std::memory_order_release)) {
          ScheduleContinuation(true);
          return;
        }
        [[fallthrough]];
      default:
        LOGIC_ASSERT(false, "State Transfer Error");
    }
  }

  bool CurrentThreadInExecutor() const {
    if (executor_) {
      return executor_->CurrentThreadInExecutor();
    }
    return false;
  }

 private:
  void ScheduleContinuation(bool trigger_by_continuation) {
    LOGIC_ASSERT(state_.load(std::memory_order_relaxed) == detail::State::kDone,
                 "FutureState is not done");
    if (!force_scheduled && (!executor_ || trigger_by_continuation || CurrentThreadInExecutor())) {
      // 立即执行
      ContinuationReference guard(this);
      continuation_(std::move(try_value_));
    } else {
      ContinuationReference guard(this);
      ContinuationReference guard_for_exception(this);
      bool ret;
      if (context_ == Executor::kNullContext) {
        ret = executor_->Schedule([ref = std::move(guard)]() mutable {
          FutureState *fs = ref.GetFutureState();
          fs->continuation_(std::move(fs->try_value_));
        });
      } else {
        ScheduleOptions options;
        options.prompt = !force_scheduled;
        ret = executor_->Checkin([ref = std::move(guard)]() mutable {
          auto fs = ref.GetFutureState();
          fs->continuation_(std::move(fs->try_value_));
        }, context_, options);
      }
      if (!ret) {
        // 调度失败，立即执行
        continuation_(std::move(try_value_));
      }
    }
  }

  void RefContinuation() {
    continuation_ref_.fetch_add(1, std::memory_order_relaxed);
  }
  void DerefContinuation() {
    auto old = continuation_ref_.fetch_sub(1, std::memory_order_relaxed);
    ASSERT(old >= 1);
    if (old == 1) {
      continuation_.~Continuation();
    }
  }

  std::atomic<detail::State> state_;
  std::atomic<uint8_t> attached_;
  std::atomic<uint8_t> continuation_ref_;
  std::atomic<std::size_t> promise_ref_;
  bool force_scheduled;

  Try<T> try_value_;
  union {
    Continuation continuation_;
  };
  Executor *executor_;
  Executor::Context context_;
};

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_FUTURE_STATE_HPP_
