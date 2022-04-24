#include <async_simple/coro/future_awaiter.hpp>

#include "async_simple_test.hpp"

#include <async_simple/coro/lazy.hpp>

namespace async_simple::coro {

class FutureAwaiterTest : public testing::Test {};

using namespace std::chrono_literals;

namespace {

template<typename Callback>
void Sum(int a, int b, Callback &&cb) {
  std::thread([a, b, cb=std::move(cb)]() mutable {
    cb(a + b);
  }).detach();
}

}

TEST_F(FutureAwaiterTest, TestWithFuture) {
  auto lazy1 = [&]() -> Lazy<> {
    Promise<int> promise;
    auto future = promise.GetFuture();
    Sum(1, 1, [promise=std::move(promise)](int val) mutable {
      promise.SetValue(val);
    });
    std::this_thread::sleep_for(500ms);
    auto val = co_await future;
    EXPECT_EQ(2, val);
  };
  SyncAwait(lazy1());
  auto lazy2 = [&]() -> Lazy<> {
    Promise<int> promise;
    auto future = promise.GetFuture();
    Sum(1, 1, [promise=std::move(promise)](int val) mutable {
      std::this_thread::sleep_for(500ms);
      promise.SetValue(val);
    });
    auto val = co_await future;
    EXPECT_EQ(2, val);
  };
  SyncAwait(lazy2());
}


} // namespace async_simple::coro
