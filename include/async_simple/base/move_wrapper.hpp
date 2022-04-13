#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_MOVE_WRAPPER_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_MOVE_WRAPPER_HPP_

#include <utility>

namespace async_simple {

/// std::function要求可调用对象是可复制构造的(copy constructible)
/// 因此可以使用MoveWrapper类来包装一个可移动构造对象，间接使得它可复制构造
/// MoveWrapper在内部使用移动来实现复制
template<typename T>
class MoveWrapper {
 public:
  MoveWrapper() = default;
  ~MoveWrapper() = default;

  MoveWrapper(T &&value) : value_(std::move(value)) {}

  MoveWrapper(const MoveWrapper &other) : value_(std::move(other.value_)) {}
  MoveWrapper(MoveWrapper &&other) : value_(std::move(other)) {}

  MoveWrapper &operator=(const MoveWrapper) = delete;
  MoveWrapper &operator=(MoveWrapper &&) = delete;

  T &Get() { return value_; }
  const T &Get() const { return value_; }

 private:
  mutable T value_;
};

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_MOVE_WRAPPER_HPP_
