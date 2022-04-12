#ifndef MINI_ASYNC_SIMPLE_TEST_COMMON_HPP_
#define MINI_ASYNC_SIMPLE_TEST_COMMON_HPP_

#include <utility>

namespace async_simple {

enum DummyState : int {
  CONSTRUCTED = 1,
  DESTRUCTED = 2,
};

struct Dummy {
  Dummy() = default;
  Dummy(int *state_) : state(state_) {
    if (state) {
      *state |= CONSTRUCTED;
    }
  }
  Dummy(Dummy &&other) : state(other.state) { other.state = nullptr; }
  Dummy &operator=(Dummy &&other) {
    std::swap(other.state, state);
    return *this;
  }
  ~Dummy() {
    if (state) {
      *state |= DESTRUCTED;
      state = nullptr;
    }
  }
  int *state = nullptr;
};

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_TEST_COMMON_HPP_
