#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_NONCOPYABLE_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_NONCOPYABLE_HPP_

namespace async_simple {

class noncopyable {
 public:
  noncopyable(const noncopyable &) = delete;
  noncopyable &operator=(const noncopyable &) = delete;
 protected:
  noncopyable() = default;
  ~noncopyable() = default;
};

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_NONCOPYABLE_HPP_
