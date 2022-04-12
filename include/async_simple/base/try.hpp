#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_TRY_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_TRY_HPP_

#include <utility>

#include "async_simple/base/macro.hpp"
#include "async_simple/base/assert.hpp"
#include "async_simple/base/noncopyable.hpp"
#include "async_simple/base/unit.hpp"

namespace async_simple {

/// Try<T>内部可以包含一个T类型的实例，一个异常，或者什么都不包含
template<typename T>
class Try : noncopyable {
  enum class InnerType {
    kValue,
    kException,
    kNothing,
  };
 public:
  Try() : inner_type_(InnerType::kNothing) {}
  ~Try() { Destroy(); }

  Try(Try &&other) : inner_type_(other.inner_type_) {
    if (inner_type_ == InnerType::kValue) {
      new(&value_) T(std::move(other.value_));
    } else if (inner_type_ == InnerType::kException) {
      new(&exception_) std::exception_ptr(other.exception_);
    }
  }
  template<typename T2 = T>
  Try(std::enable_if_t<std::is_same_v<T2, Unit>, const Try<void> &> other) {
    if (other.HasException()) {
      inner_type_ = InnerType::kException;
      new(&exception_) std::exception_ptr(other.exception_);
    } else {
      inner_type_ = InnerType::kValue;
      new(&value_) T();
    }
  }
  Try(const T &val) : inner_type_(InnerType::kValue), value_(val) {}
  Try(T &&val) : inner_type_(InnerType::kValue), value_(std::move(val)) {}
  Try(std::exception_ptr exception) : inner_type_(InnerType::kException), exception_(std::move(exception)) {}

  Try &operator=(Try &&other) {
    if (&other == this) return *this;
    Destroy();
    inner_type_ = other.inner_type_;
    if (inner_type_ == InnerType::kValue) {
      new(&value_) T(std::move(other.value_));
    } else if (inner_type_ == InnerType::kException) {
      new(&exception_) std::exception_ptr(other.exception_);
    }
    return *this;
  }
  Try &operator=(const std::exception_ptr &exception) {
    if (inner_type_ == InnerType::kException && exception == exception_) {
      return *this;
    }
    Destroy();
    inner_type_ = InnerType::kException;
    new(&exception_) std::exception_ptr(exception);
    return *this;
  }

  [[nodiscard]] bool Available() const { return inner_type_ != InnerType::kNothing; }
  [[nodiscard]] bool HasException() const { return inner_type_ == InnerType::kException; }
  const T &Value() const &{
    CheckHoldsValue();
    return value_;
  }
  T &Value() &{
    CheckHoldsValue();
    return value_;
  }
  T &&Value() &&{
    CheckHoldsValue();
    return std::move(value_);
  }
  const T &&Value() const &&{
    CheckHoldsValue();
    return std::move(value_);
  }

  void SetException(const std::exception_ptr &exception) {
    if (inner_type_ == InnerType::kException && exception_ == exception) {
      return;
    }
    Destroy();
    inner_type_ = InnerType::kException;
    new(&exception_) std::exception_ptr(exception);
  }
  std::exception_ptr GetException() {
    LOGIC_ASSERT(inner_type_ == InnerType::kException, "Try object do not have an error");
    return exception_;
  }

 private:
  FORCE_INLINE void CheckHoldsValue() const {
    if (inner_type_ == InnerType::kValue) LIKELY {
      return;
    } else if (inner_type_ == InnerType::kException) {
      std::rethrow_exception(exception_);
    } else if (inner_type_ == InnerType::kNothing) {
      throw std::logic_error("Try object is empty");
    } else {
      ASSERT(false);
    }
  }

  void Destroy() {
    if (inner_type_ == InnerType::kValue) {
      value_.~T();
    } else if (inner_type_ == InnerType::kException) {
      exception_.~exception_ptr();
    }
    inner_type_ = InnerType::kNothing;
  }

  friend Try<Unit>;

  InnerType inner_type_;
  union {
    T value_;
    std::exception_ptr exception_;
  };
};

template<>
class Try<void> {
 public:
  Try() = default;
  Try(std::exception_ptr exception) : exception_(std::move(exception)) {}
  Try &operator=(std::exception_ptr exception) {
    exception_ = std::move(exception);
    return *this;
  }

  Try(Try &&other) : exception_(std::move(other.exception_)) {}
  Try &operator=(Try &&other) {
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
  friend Try<Unit>;

  std::exception_ptr exception_;
};

// T不为void
template<typename F, typename ...Args>
requires (!std::is_same_v<std::invoke_result_t<F, Args...>, void>)
Try<std::invoke_result_t<F, Args...>>
MakeTryCall(F &&f, Args &&...args) {
  using T = std::invoke_result_t<F, Args...>;
  try {
    return Try<T>(std::forward<F>(f)(std::forward<Args>(args)...));
  } catch (...) {
    return Try<T>(std::current_exception());
  }
}

// T为void
template<typename F, typename ...Args>
requires std::is_same_v<std::invoke_result_t<F, Args...>, void>
Try<void>
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

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_TRY_HPP_
