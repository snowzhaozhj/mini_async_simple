#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_FUTURE_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_FUTURE_HPP_

#include "async_simple/sync/future_state.hpp"
#include "async_simple/sync/future_trait.hpp"
#include "async_simple/sync/local_state.hpp"
#include "async_simple/sync/promise.hpp"

namespace async_simple {

template<typename T>
class Future : noncopyable {
 public:
  using value_type = T;

  Future(FutureState<T> *fs) : shared_state_(fs) {
    if (shared_state_) {
      shared_state_->AttachOne();
    }
  }
  Future(Try<T> &&t) : shared_state_(nullptr) {}

  ~Future() {
    if (shared_state_) {
      shared_state_->DetachOne();
    }
  }

  Future(Future &&other)
      : shared_state_(std::exchange(other.shared_state_, nullptr)),
        local_state_(std::move(other.local_state_)) {
  }

  Future &operator=(Future &&other) {
    if (this != &other) {
      shared_state_ = std::exchange(other.shared_state_, nullptr);
      local_state_ = std::move(other.local_state_);
    }
    return *this;
  }

  [[nodiscard]] bool Valid() const {
    return shared_state_ != nullptr || local_state_.HasResult();
  }
  bool HasResult() const {
    return local_state_.HasResult() || shared_state_->HasResult();
  }

  Try<T> &&Result() &&{ return std::move(GetTry(*this)); }
  Try<T> &Result() &{ return GetTry(*this); }
  const Try<T> &Result() const &{ return GetTry(*this); }

  T &&Value() &&{ return std::move(Result().Value()); };
  T &Value() &{ return Result().Value(); }
  const T &Value() const &{ return Result().Value(); }

  void SetExecutor(Executor *executor) {
    if (shared_state_) {
      shared_state_->SetExecutor(executor);
    } else {
      local_state_.SetExecutor(executor);
    }
  }
  Executor *GetExecutor() {
    if (shared_state_) {
      return shared_state_->GetExecutor();
    } else {
      return local_state_.GetExecutor();
    }
  }

  template<typename F>
  void SetContinuation(F &&func) {
    ASSERT(Valid());
    if (shared_state_) {
      shared_state_->SetContinuation(std::forward<F>(func));
    } else {
      local_state_.SetContinuation(std::forward<F>(func));
    }
  }

  bool CurrentThreadInExecutor() {
    ASSERT(Valid());
    if (shared_state_) {
      return shared_state_->CurrentThreadInExecutor();
    } else {
      return local_state_.CurrentThreadInExecutor();
    }
  }

  /// 阻塞当前线程，直到Future成功得到值
  /// @note 只能在右值上调用
  T Get() &&{
    Wait();
    return (std::move(*this)).Value();
  }
  void Wait() {
    LOGIC_ASSERT(Valid(), "Future is borken");
    if (HasResult()) {
      return;
    }
    /// 在同一个Executor中等待可能会导致死锁
    ASSERT(!CurrentThreadInExecutor());

    Promise<T> promise;
    auto future = promise.GetFuture();
    shared_state_->SetExecutor(nullptr);  // 下一个continuation是简单的，直接立即执行
    std::binary_semaphore sem(0);
    shared_state_->SetContinuation([&sem, p = std::move(promise)](Try<T> &&t) {
      p.SetValue(std::move(t));
      sem.release();
    });
    sem.acquire();
    *this = std::move(future);
    ASSERT(shared_state_->HasResult());
  }

  /// 为future设置executor, 只能用在右值上
  Future<T> Via(Executor *executor) &&{
    SetExecutor(executor);
    return std::move(*this);
  }

  /// F是一个以Try<T>&&为参数的回调函数
  template<typename F, typename R = TryCallableResult<T, F>>
  Future<typename R::ReturnsFuture::Inner>
  ThenTry(F &&f) &&{
    return ThenImpl<F, R>(std::forward<F>(f));
  }

  /// F是一个以T &&为参数的回调函数
  /// 如果抛出异常，F将不会被调用
  template<typename F, typename R = ValueCallableResult<T, F>>
  Future<typename R::ReturnsFuture::Inner>
  ThenValue(F &&f) &&{
    auto lambda = [func = std::forward<F>(f)](Try<T> &&t) mutable {
      return std::forward<F>(func)(std::move(t).Value());
    };
    using Func = decltype(lambda);
    return ThenImpl<Func, TryCallableResult<T, Func>>(std::move(lambda));
  }

 private:
  template<typename Clazz>
  static decltype(auto) GetTry(Clazz &self) {
    LOGIC_ASSERT(self.Valid(), "Future is broken");
    LOGIC_ASSERT(self.local_state_.HasResult() || self.shared_state->HasResult(),
                 "Future is not ready");
    if (self.shared_state_) {
      return self.shared_state_->GetTry();
    } else {
      return self.local_state_.GetTry();
    }
  }

  template<typename F, typename R>
  requires R::ReturnsFuture::value
  Future<typename R::ReturnsFuture::Inner>
  ThenImpl(F &&func) {
    LOGIC_ASSERT(Valid(), "Future is broken");
    using T2 = typename R::ReturnsFuture::Inner;

    if (!shared_state_) {
      try {
        auto new_future = std::forward<F>(func)(std::move(local_state_.GetTry()));
        if (!new_future.GetExecutor()) {
          new_future.SetExecutor(local_state_.GetExecutor());
        }
        return new_future;
      } catch (...) {
        return Future<T2>(Try<T2>(std::current_exception()));
      }
    }

    Promise<T2> promise;
    auto new_future = promise.GetFuture();
    new_future.SetExecutor(shared_state_->GetExecutor());
    shared_state_->SetContinuation([p = std::move(promise),
                                       f = std::forward<F>(func)](Try<T> &&t) {
      if (!R::is_try && t.HasException()) {
        p.SetException(t.GetException());
      } else {
        try {
          auto f2 = f(std::move(t));
          f2.SetContinuation([pm = std::move(p)](Try<T2> &&t2) {
            pm.SetValue(std::move(t2));
          });
        } catch (...) {
          p.SetException(std::current_exception());
        }
      }
    });
    return new_future;
  }

  template<typename F, typename R>
  requires (!R::ReturnsFuture::value)
  Future<typename R::ReturnsFuture::Inner>
  ThenImpl(F &&func) {
    LOGIC_ASSERT(Valid(), "Future is broken");
    using T2 = typename R::ReturnsFuture::Inner;

    if (!shared_state_) {
      Future<T2> new_future = std::forward<F>(func)(std::move(local_state_.GetTry()));
      new_future.SetExecutor(local_state_.GetExecutor());
      return new_future;
    }

    Promise<T2> promise;
    auto new_future = promise.GetFuture();
    new_future.SetExecutor(shared_state_->GetExecutor());
    shared_state_->SetContinuation([p = std::move(promise),
                                       f = std::forward<F>(func)](Try<T> &&t) {
      if (!R::is_try && t.HasException()) {
        p.SetException(t.GetException());
      } else {
        p.SetValue(MakeTryCall(std::forward<F>(f), std::move(t)));
      }
    });
    return new_future;
  }

  FutureState<T> *shared_state_;
  LocalState<T> local_state_;
};

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_FUTURE_HPP_
