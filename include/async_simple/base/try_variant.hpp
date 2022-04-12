#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_TRY_VARIANT_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_TRY_VARIANT_HPP_

/// 使用std::variant来实现Try

#include "async_simple/base/macro.hpp"
#include "async_simple/base/assert.hpp"
#include "async_simple/base/noncopyable.hpp"
#include "async_simple/base/unit.hpp"

#include <variant>

namespace async_simple {

template<typename T>
class TryV : noncopyable {
  using value_type = std::variant<std::monostate, T, std::exception_ptr>;
 public:
  TryV() = default;
  ~TryV() = default;

  TryV(TryV &&other) : value_(std::move(other.value_)) {}
  template<typename T2 = T>
  TryV(std::enable_if_t<std::is_same_v<T2, Unit>, const TryV<void> &> other) {
    if (other.HasException()) {
      value_ = other.GetException();
    } else {
      value_ = T{};
    }
  }
  TryV(const T &value) : value_(value) {}
  TryV(T &&value) : value_(std::move(value)) {}
  TryV(std::exception_ptr exception) : value_(exception) {}

  TryV &operator=(TryV &&other) {
    if (&other != this) {
      value_ = std::move(other.value_);
    }
    return *this;
  }
  TryV &operator=(const std::exception_ptr &value) {
    value_ = value;
    return *this;
  }

  [[nodiscard]] bool Available() const {
    return !std::holds_alternative<std::monostate>(value_);
  }
  [[nodiscard]] bool HasException() const {
    return std::holds_alternative<std::exception_ptr>(value_);
  }
  const T &Value() const &{
    CheckHoldsValue();
    return std::get<T>(value_);
  }
  T &Value() &{
    CheckHoldsValue();
    return std::get<T>(value_);
  }
  T &&Value() &&{
    CheckHoldsValue();
    return std::move(std::get<T>(value_));
  }
  const T &&Value() const &&{
    CheckHoldsValue();
    return std::move(std::get<T>(value_));
  }

  void SetException(const std::exception_ptr &exception) {
    if (std::holds_alternative<std::exception_ptr>(value_) &&
        std::get<std::exception_ptr>(value_) == exception) {
      return;
    }
    value_ = exception;
  }
  std::exception_ptr GetException() {
    LOGIC_ASSERT(std::holds_alternative<std::exception_ptr>(value_),
                 "Try object do not have an exception");
    return std::get<std::exception_ptr>(value_);
  }

 private:
  FORCE_INLINE void CheckHoldsValue() const {
    if (std::holds_alternative<T>(value_)) LIKELY {
      return;
    } else if (std::holds_alternative<std::exception_ptr>(value_)) {
      std::rethrow_exception(std::get<std::exception_ptr>(value_));
    } else if (std::holds_alternative<std::monostate>(value_)) {
      throw std::logic_error("Try object is empty");
    } else {
      ASSERT(false);
    }
  }

  friend TryV<Unit>;

  value_type value_;
};

template<>
class TryV<void> {
 public:
  TryV() = default;
  TryV(std::exception_ptr exception) : exception_(std::move(exception)) {}
  TryV &operator=(std::exception_ptr exception) {
    exception_ = std::move(exception);
    return *this;
  }

  TryV(TryV &&other) : exception_(std::move(other.exception_)) {}
  TryV &operator=(TryV &&other) {
    if (this != &other) {
      std::swap(exception_, other.exception_);
    }
    return *this;
  }

  void Value() {
    if (exception_) {
      std::rethrow_exception(exception_);
    }
  }

  [[nodiscard]] bool HasException() const { return exception_.operator bool(); }
  void SetException(std::exception_ptr exception) {
    exception_ = std::move(exception);
  }
  std::exception_ptr GetException() { return exception_; }

 private:
  friend TryV<Unit>;

  std::exception_ptr exception_;
};

// T不为void
template<typename F, typename ...Args>
requires (!std::is_same_v<std::invoke_result_t<F, Args...>, void>)
TryV<std::invoke_result_t<F, Args...>>
MakeTryVCall(F &&f, Args &&...args) {
  using T = std::invoke_result_t<F, Args...>;
  try {
    return TryV<T>(std::forward<F>(f)(std::forward<Args>(args)...));
  } catch (...) {
    return TryV<T>(std::current_exception());
  }
}

// T为void
template<typename F, typename ...Args>
requires std::is_same_v<std::invoke_result_t<F, Args...>, void>
TryV<void>
MakeTryCall(F &&f, Args &&...args) {
  using T = std::invoke_result_t<F, Args...>;
  try {
    std::forward<F>(f)(std::forward<Args>(args)...);
    return {};
  } catch (...) {
    return {std::current_exception()};
  }
}

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_TRY_VARIANT_HPP_
