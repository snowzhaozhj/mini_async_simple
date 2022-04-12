#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_UNIT_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_UNIT_HPP_

namespace async_simple {

/// 当我们无法使用void类型时，我们可以把Unit作为一个最简单的类型来使用
struct Unit {
  constexpr bool operator==(const Unit &) const { return true; }
  constexpr bool operator!=(const Unit &) const { return false; }
};

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_UNIT_HPP_
