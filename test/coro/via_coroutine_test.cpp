#include <async_simple/coro/via_coroutine.hpp>

#include "async_simple_test.hpp"

#include <async_simple/coro/lazy.hpp>
#include <async_simple/executor/simple_executor.hpp>

namespace async_simple::coro {

class ViaCoroutineTest : public testing::Test {};

std::atomic<int> check{0};
class TrackedSimpleExecutor : public executors::SimpleExecutor {
 public:
  TrackedSimpleExecutor(size_t tn) : SimpleExecutor(tn) {}
  Context Checkout() override {
    check++;
    return SimpleExecutor::Checkout();
  }
  bool Checkin(Func func, Context ctx, ScheduleOptions opts) override {
    // -1 is invalid ctx for SimpleExecutor
    if (ctx == (void *) -1) {
      return false;
    }
    check--;
    return SimpleExecutor::Checkin(func, ctx, opts);
  }
};

struct Awaiter {
  bool await_ready() noexcept { return false; }
  bool await_suspend(std::coroutine_handle<> continuation) noexcept { return false; }
  void await_resume() noexcept {}
};

TEST_F(ViaCoroutineTest, SimpleCheckEQ) {
  TrackedSimpleExecutor e1(10);
  auto Task = [&]() -> Lazy<> {
    co_await Awaiter{};
  };
  SyncAwait(Task().Via(&e1));
  EXPECT_EQ(check.load(), 0);
}

} // namespace async_simple::coro
