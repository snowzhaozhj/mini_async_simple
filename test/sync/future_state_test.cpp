#include <async_simple/sync/future_state.hpp>

#include "async_simple_test.hpp"
#include "common.hpp"

#include <async_simple/executor/simple_executor.hpp>

namespace async_simple {

class FutureStateTest : public testing::Test {};

TEST_F(FutureStateTest, TestSimpleProcess) {
  auto fs = new FutureState<int>();
  fs->AttachOne();
  ASSERT_FALSE(fs->HasResult());
  ASSERT_FALSE(fs->HasContinuation());
  ASSERT_FALSE(fs->GetExecutor());

  fs->SetResult(Try<int>(100));
  ASSERT_TRUE(fs->HasResult());
  ASSERT_FALSE(fs->HasContinuation());
  auto &v = fs->GetTry();
  ASSERT_EQ(v.Value(), 100);

  int output = 0;
  fs->SetContinuation([&output](Try<int> &&v) { output = v.Value() + 5; });
  ASSERT_TRUE(fs->HasResult());
  ASSERT_TRUE(fs->HasContinuation());

  ASSERT_EQ(output, 105);

  fs->DetachOne();
}

TEST_F(FutureStateTest, TestSimpleExecutor) {
  auto fs = new FutureState<int>();
  fs->AttachOne();
  auto executor = new executors::SimpleExecutor(5);
  fs->SetExecutor(executor);

  ASSERT_FALSE(fs->HasResult());
  ASSERT_FALSE(fs->HasContinuation());
  ASSERT_TRUE(fs->GetExecutor());

  fs->SetResult(Try<int>(100));
  ASSERT_TRUE(fs->HasResult());
  ASSERT_FALSE(fs->HasContinuation());
  auto &v = fs->GetTry();
  ASSERT_EQ(v.Value(), 100);

  int output = 0;
  fs->SetContinuation([&output](Try<int> &&v) { output = v.Value() + 5; });
  ASSERT_TRUE(fs->HasResult());
  ASSERT_TRUE(fs->HasContinuation());

  delete executor;
  ASSERT_EQ(output, 105);

  fs->DetachOne();
}

TEST_F(FutureStateTest, TestClass) {
  auto fs = new FutureState<Dummy>();
  fs->AttachOne();
  auto executor = new executors::SimpleExecutor(5);
  fs->SetExecutor(executor);

  ASSERT_FALSE(fs->HasResult());
  ASSERT_FALSE(fs->HasContinuation());
  EXPECT_TRUE(fs->GetExecutor());

  int state = 0;
  Try<Dummy> v(&state);
  fs->SetResult(std::move(v));
  ASSERT_TRUE(fs->HasResult());
  ASSERT_FALSE(fs->HasContinuation());
  ASSERT_TRUE(fs->GetTry().Value().state);

  int *output = nullptr;
  Dummy nocopy(nullptr);
  fs->SetContinuation([&output, d = std::move(nocopy)](Try<Dummy> &&v) {
    auto local_v = std::move(v);
    output = local_v.Value().state;
    (void) d;
  });
  EXPECT_TRUE(fs->HasResult());
  EXPECT_TRUE(fs->HasContinuation());

  delete executor;
  EXPECT_EQ(&state, output);
  EXPECT_FALSE(fs->GetTry().Value().state);
  fs->DetachOne();
}

} // namespace async_simple
