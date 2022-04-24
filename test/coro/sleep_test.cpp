#include <async_simple/coro/sleep.hpp>

#include "async_simple_test.hpp"

#include <async_simple/executor/simple_executor.hpp>

#include <iostream>

using namespace std::chrono_literals;

namespace async_simple::coro {

class SleepTest : public testing::Test {};

TEST_F(SleepTest, TestSleep) {
  executors::SimpleExecutor e1(5);
  auto sleep_task = [&]() -> Lazy<> {
    auto current = co_await CurrentExecutor{};
    EXPECT_EQ(&e1, current);

    auto start_time = std::chrono::system_clock::now();
    co_await coro::sleep(1s);
    auto end_time = std::chrono::system_clock::now();

    current = co_await CurrentExecutor{};
    EXPECT_EQ(&e1, current);

    auto duration = end_time - start_time;

    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << std::endl;
  };
  SyncAwait(sleep_task().Via(&e1));

  auto sleep_task2 = [&]() -> Lazy<> {
    auto current = co_await CurrentExecutor{};
    EXPECT_EQ(&e1, current);

    auto start_time = std::chrono::system_clock::now();
    co_await coro::sleep(900ms);
    auto end_time = std::chrono::system_clock::now();

    current = co_await CurrentExecutor{};
    EXPECT_EQ(&e1, current);

    auto duration = end_time - start_time;

    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << std::endl;
  };
  SyncAwait(sleep_task2().Via(&e1));
}

} // namespace async_simple::coro
