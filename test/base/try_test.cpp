#include <async_simple/base/try.hpp>

#include "async_simple_test.hpp"
#include "common.hpp"

namespace async_simple {

class TryTest : public testing::Test {};

TEST_F(TryTest, TestSimpleProcess) {
  Try<int> v0(1);
  ASSERT_EQ(1, v0.Value());

  Try<int> v1 = 1;
  ASSERT_EQ(1, v1.Value());

  Try<int> v2 = std::move(v0);
  ASSERT_EQ(1, v2.Value());

  Try<int> v3(std::move(v1));
  ASSERT_TRUE(v3.Available());
  ASSERT_FALSE(v3.HasException());
  ASSERT_EQ(1, v3.Value());

  Try<int> v4;
  ASSERT_FALSE(v4.Available());

  bool hasException = false;
  Try<int> ve;
  try {
    throw "abcdefg";
  } catch (...) {
    ve = std::current_exception();
  }

  ASSERT_TRUE(ve.Available());
  ASSERT_TRUE(ve.HasException());

  try {
    ve.Value();
  } catch (...) {
    hasException = true;
  }
  ASSERT_TRUE(hasException);

  Try<int> emptyV;
  ASSERT_FALSE(emptyV.Available());
  emptyV = 100;
  ASSERT_TRUE(emptyV.Available());
  ASSERT_FALSE(emptyV.HasException());
  ASSERT_EQ(100, emptyV.Value());
}

TEST_F(TryTest, TestClass) {
  int state0 = 0;
  Try<Dummy> v0{Dummy(&state0)};
  EXPECT_TRUE(v0.Available());
  EXPECT_FALSE(v0.HasException());
  EXPECT_TRUE(state0 & CONSTRUCTED);
  EXPECT_FALSE(state0 & DESTRUCTED);
  std::exception_ptr error;
  v0 = error;
  EXPECT_TRUE(v0.HasException());
  EXPECT_TRUE(state0 & CONSTRUCTED);
  EXPECT_TRUE(state0 & DESTRUCTED);
}

TEST_F(TryTest, TestVoid) {
  Try<void> v;
  bool hasException = false;
  std::exception_ptr error;
  v.SetException(std::make_exception_ptr(std::runtime_error("")));
  try {
    v.Value();
  } catch (...) {
    hasException = true;
    error = std::current_exception();
  }
  ASSERT_TRUE(hasException);
  ASSERT_TRUE(v.HasException());

  Try<void> ve = error;
  ASSERT_TRUE(ve.HasException());
}

} // namespace async_simple
