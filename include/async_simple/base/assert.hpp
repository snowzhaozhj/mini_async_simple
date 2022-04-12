#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_ASSERT_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_ASSERT_HPP_

#include <cassert>
#include <stdexcept>

// ASSERT失败时，表明库中存在问题
#define ASSERT(expr) assert(expr)
// LOGIC_ASSERT失败时，表明用户代码中存在问题
#define LOGIC_ASSERT(expr, msg) \
  do {                          \
    if (!(expr)) UNLIKELY {     \
        throw std::logic_error(msg);  \
    }                           \
  } while (false)

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_BASE_ASSERT_HPP_
