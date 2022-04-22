#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_PROMISE_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_PROMISE_HPP_

#include "async_simple/sync/future_state.hpp"

namespace async_simple {

template<typename T>
class Future;

template<typename T>
class Promise {
 public:
  Promise() : shared_state_(new FutureState<T>()), has_future_(false) {
    shared_state_->AttachPromise();
  }
  ~Promise() {
    if (shared_state_) {
      shared_state_->DetachPromise();
    }
  }
  Promise(const Promise &other)
      : shared_state_(other.shared_state_),
        has_future_(other.has_future_) {
    shared_state_->AttachPromise();
  }
  Promise &operator=(const Promise &other) {
    if (this != &other) {
      this->~Promise();
      shared_state_ = other.shared_state_;
      has_future_ = other.has_future_;
      shared_state_->AttachPromise();
    }
    return *this;
  }

  Promise(Promise &&other)
      : shared_state_(std::exchange(other.shared_state_, nullptr)),
        has_future_(std::exchange(other.has_future_, false)) {
  }

  Promise &operator=(Promise &&other) {
    shared_state_ = std::exchange(other.shared_state_, nullptr);
    has_future_ = std::exchange(other.has_future_, false);
    return *this;
  }

  bool Valid() const { return shared_state_ != nullptr; }

  Future<T> GetFuture() {
    LOGIC_ASSERT(Valid(), "Promise is broken");
    LOGIC_ASSERT(!has_future_, "Promise already has a future");
    has_future_ = true;
    return Future<T>(shared_state_);
  }

  Promise &Checkout() {
    if (shared_state_) {
      shared_state_->Checkout();
    }
    return *this;
  }

  Promise &ForceScheduled() {
    if (shared_state_) {
      shared_state_->SetForceScheduled(true);
    }
    return *this;
  }

  void SetException(std::exception_ptr exception) {
    LOGIC_ASSERT(Valid(), "Promise is broken");
    shared_state_->SetResult(Try<T>(exception));
  }
  void SetValue(T &&v) {
    LOGIC_ASSERT(Valid(), "Promise is broken");
    shared_state_->SetResult(Try<T>(std::forward<T>(v)));
  }
  void SetValue(Try<T> &&t) {
    LOGIC_ASSERT(Valid(), "Promise is broken");
    shared_state_->SetResult(std::move(t));
  }

 private:
  FutureState<T> *shared_state_{nullptr};
  bool has_future_{false};
};

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_PROMISE_HPP_
