#include <async_simple/coro/lazy.hpp>

#include "async_simple_test.hpp"
#include "scoped_bench.hpp"

#include <async_simple/executor/simple_executor.hpp>
#include <async_simple/coro/collect.hpp>

using namespace std::chrono_literals;

namespace async_simple::coro {

#define CHECK_EXECUTOR(ex)                            \
    do {                                              \
        EXPECT_TRUE((ex)->CurrentThreadInExecutor()); \
        auto current = co_await CurrentExecutor{};    \
        EXPECT_EQ((ex), current);                     \
    } while (0)

class LazyTest : public testing::Test {
 public:
  LazyTest() : executor_(1) {}

  void SetUp() override {
    std::lock_guard lg(mutex_);
    value_ = 0;
    done_ = false;
  }

  void TearDown() override {}

  void ApplyValue(std::function<void(int)> f) {
    std::thread([this, f = std::move(f)]() {
      std::unique_lock ul(mutex_);
      cv_.wait(ul, [this]() { return done_; });
      f(value_);
    }).detach();
  }

  Lazy<int> GetValue() {
    struct ValueAwaiter {
      ValueAwaiter(LazyTest *t) : test(t) {}

      bool await_ready() { return false; }
      void await_suspend(std::coroutine_handle<> continuation) noexcept {
        test->ApplyValue([this, continuation](int v) mutable {
          value = v;
          continuation.resume();
        });
      }
      int await_resume() noexcept { return value; }

      LazyTest *test;
      int value;
    };

    co_return co_await ValueAwaiter(this);
  }

  Lazy<void> TestException() {
    throw std::runtime_error("Test Exception");
    co_return;
  }

  Lazy<void> MakeVoidTask() {
    struct ValueAwaiter {
      ValueAwaiter() {}

      bool await_ready() { return false; }
      void await_suspend(std::coroutine_handle<> continuation) noexcept {
        std::thread([continuation]() mutable {
          continuation.resume();
        }).detach();
      }
      void await_resume() noexcept {}
    };

    auto id1 = std::this_thread::get_id();
    co_await ValueAwaiter{};
    auto id2 = std::this_thread::get_id();
    EXPECT_EQ(id1, id2);
  }

  template<typename T>
  Lazy<T> GetValue(T x) {
    struct ValueAwaiter {
      ValueAwaiter(T v) : value(v) {}

      bool await_ready() { return false; }
      void await_suspend(std::coroutine_handle<> continuation) noexcept {
        std::thread([continuation]() mutable {
          continuation.resume();
        }).detach();
      }
      T await_resume() noexcept { return value; }

      T value;
    };

    auto id1 = std::this_thread::get_id();
    auto ret = co_await ValueAwaiter(x);
    auto id2 = std::this_thread::get_id();
    EXPECT_EQ(id1, id2);
    co_return ret;
  }

  template<bool thread_id = false>
  Lazy<int> GetValueWithSleep(int x) {
    struct ValueAwaiter {
      ValueAwaiter(int v) : value(v) {}

      bool await_ready() { return false; }
      void await_suspend(std::coroutine_handle<> continuation) noexcept {
        std::thread([continuation]() mutable {
          std::this_thread::sleep_for(
              std::chrono::microseconds(rand() % 1000 + 1));
          continuation.resume();
        }).detach();
      }
      int await_resume() noexcept { return value; }

      int value;
    };

    auto id1 = std::this_thread::get_id();
    auto ret = co_await ValueAwaiter(x);
    auto id2 = std::this_thread::get_id();
    EXPECT_EQ(id1, id2);
    co_return ret;
  }

  Lazy<std::thread::id> GetThreadId() {
    struct ValueAwaiter {
      ValueAwaiter() {}
      bool await_ready() { return false; }
      void await_suspend(std::coroutine_handle<> continuation) noexcept {
        std::thread([continuation]() mutable {
          std::this_thread::sleep_for(
              std::chrono::microseconds(rand() % 1000000 + 1));
          continuation.resume();
        }).detach();
      }
      void await_resume() noexcept {}
    };

    auto id1 = std::this_thread::get_id();
    co_await ValueAwaiter();
    auto id2 = std::this_thread::get_id();
    EXPECT_EQ(id1, id2);
    co_return id1;
  }

  Lazy<int> PlusOne() {
    int v = co_await GetValue();
    co_return v + 1;
  }

  Lazy<int> TestFunc() { co_return 3; }

  void TriggerValue(int val) {
    std::unique_lock ul(mutex_);
    value_ = val;
    done_ = true;
    cv_.notify_one();
  }

  executors::SimpleExecutor executor_;

  std::mutex mutex_;
  std::condition_variable cv_;
  int value_;
  bool done_;
};

TEST_F(LazyTest, TestSimpleAsync) {
  auto test = [this]() -> Lazy<int> {
    CHECK_EXECUTOR(&executor_);
    auto ret = co_await PlusOne();
    CHECK_EXECUTOR(&executor_);
    co_return ret;
  };

  TriggerValue(100);
  int value = SyncAwait(test().Via(&executor_));
  EXPECT_EQ(value, 101);
}

TEST_F(LazyTest, TestVia) {
  auto test = [this]() -> Lazy<int> {
    auto tid = std::this_thread::get_id();
    auto ret = co_await PlusOne();
    EXPECT_TRUE(executor_.CurrentThreadInExecutor());
    EXPECT_TRUE(tid == std::this_thread::get_id());
    co_return ret;
  };

  TriggerValue(100);
  auto ret = SyncAwait(test().Via(&executor_));
  EXPECT_EQ(101, ret);
}

TEST_F(LazyTest, TestNoVia) {
  auto test = [this]() -> Lazy<int> { co_return co_await TestFunc(); };
  EXPECT_EQ(3, SyncAwait(test()));
}

TEST_F(LazyTest, TestYield) {
  std::mutex m1;
  std::mutex m2;
  int value1 = 0;
  int value2 = 0;
  m1.lock();
  m2.lock();

  auto test1 = [](std::mutex &m, int &value) -> Lazy<void> {
    m.lock();
    // push task to queue's tail
    co_await Yield();
    value++;
    co_return;
  };

  auto test2 = [](std::mutex &m, int &value) -> Lazy<void> {
    m.lock();
    value++;
    co_return;
  };

  test1(m1, value1).Via(&executor_).Start([](Try<void> result) {});
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(0, value1);

  test2(m2, value2).Via(&executor_).Start([](Try<void> result) {});
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(0, value2);

  m1.unlock();
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(0, value1);
  EXPECT_EQ(0, value2);

  m2.unlock();
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(1, value1);
  EXPECT_EQ(1, value2);

  m1.unlock();
  m2.unlock();
}

TEST_F(LazyTest, TestVoid) {
  std::atomic<int> value = 0;
  auto test = [this, &value]() -> Lazy<> {
    CHECK_EXECUTOR(&executor_);
    auto ret = co_await PlusOne();
    CHECK_EXECUTOR(&executor_);
    value = ret + 10;
  };
  TriggerValue(100);
  SyncAwait(test().Via(&executor_));
  EXPECT_EQ(111, value.load());
}

TEST_F(LazyTest, TestReadyCoro) {
  auto add_one = [](int x) -> Lazy<int> { co_return x + 1; };
  auto solve = [add_one = std::move(add_one)](int x) -> Lazy<int> {
    int tmp = co_await add_one(x);
    co_return 1 + co_await add_one(tmp);
  };
  EXPECT_EQ(10, SyncAwait(solve(7)));
}
TEST_F(LazyTest, TestExecutor) {
  executors::SimpleExecutor e2(1);
  auto add_two = [&](int x) -> Lazy<int> {
    CHECK_EXECUTOR(&e2);
    auto tmp = co_await GetValue(x);
    CHECK_EXECUTOR(&e2);
    co_return tmp + 2;
  };
  auto test = [&, this]() -> Lazy<int> {
    CHECK_EXECUTOR(&executor_);
    int y = co_await PlusOne();
    CHECK_EXECUTOR(&executor_);
    int z = co_await add_two(y).Via(&e2);
    CHECK_EXECUTOR(&executor_);
    co_return z;
  };

  TriggerValue(100);
  auto val = SyncAwait(test().Via(&executor_));
  EXPECT_EQ(val, 103);
}

TEST_F(LazyTest, TestNoCopy) {
  struct NoCopy : noncopyable {
    NoCopy() : val(-1) {}
    NoCopy(int v) : val(v) {}
    NoCopy(NoCopy &&other) : val(other.val) { other.val = -1; }
    NoCopy &operator=(NoCopy &&other) {
      val = other.val;
      return *this;
    }

    int val;
  };

  auto coro0 = []() -> Lazy<NoCopy> { co_return 10; };
  EXPECT_EQ(10, SyncAwait(coro0()).val);
}

TEST_F(LazyTest, TestDetachedCoroutine) {
  std::atomic<int> value = 0;
  auto test = [&]() -> Lazy<> {
    CHECK_EXECUTOR(&executor_);
    auto ret = co_await PlusOne();
    CHECK_EXECUTOR(&executor_);
    value = ret + 10;
    value.notify_one();
  };
  TriggerValue(100);
  test().Via(&executor_).Start([](Try<void> result) {});
  value.wait(0);
  EXPECT_EQ(value, 111);
}

TEST_F(LazyTest, TestCollectAll) {
  executors::SimpleExecutor e1(2);
  executors::SimpleExecutor e2(5);
  executors::SimpleExecutor e3(5);
  auto test = [this, &e1]() -> Lazy<int> {
    std::vector<Lazy<int>> input;
    input.push_back(GetValue(1));
    input.push_back(GetValue(2));
    CHECK_EXECUTOR(&e1);
    auto combined_lazy = CollectAll(std::move(input));
    CHECK_EXECUTOR(&e1);

    auto out = co_await std::move(combined_lazy);

    CHECK_EXECUTOR(&e1);
    EXPECT_EQ(2u, out.size());
    co_await CurrentExecutor{};
    co_return out[0].Value() + out[1].Value();
  };
  EXPECT_EQ(3, SyncAwait(test().Via(&e1)));

  auto test1 = [this, &e1]() -> Lazy<> {
    std::vector<Lazy<>> input;
    input.push_back(MakeVoidTask());
    input.push_back(MakeVoidTask());
    CHECK_EXECUTOR(&e1);
    auto combined_lazy = CollectAll(std::move(input));
    CHECK_EXECUTOR(&e1);

    auto out = co_await std::move(combined_lazy);

    CHECK_EXECUTOR(&e1);
    EXPECT_EQ(2u, out.size());
    co_await CurrentExecutor();
  };
  SyncAwait(test1().Via(&e1));

  auto test2 = [this, &e1, &e2, &e3]() -> Lazy<int> {
    std::vector<RescheduleLazy<int>> input;
    input.push_back(GetValue(1).Via(&e1));
    input.push_back(GetValue(2).Via(&e2));

    CHECK_EXECUTOR(&e3);
    auto combined_lazy = CollectAll(std::move(input));
    CHECK_EXECUTOR(&e3);

    auto out = co_await std::move(combined_lazy);

    CHECK_EXECUTOR(&e3);
    EXPECT_EQ(2u, out.size());
    co_return out[0].Value() + out[1].Value();
  };
  EXPECT_EQ(3, SyncAwait(test2().Via(&e3)));

  auto test3 = [this, &e1, &e2, &e3]() -> Lazy<> {
    std::vector<RescheduleLazy<>> input;
    input.push_back(MakeVoidTask().Via(&e1));
    input.push_back(MakeVoidTask().Via(&e2));

    CHECK_EXECUTOR(&e3);
    auto combined_lazy = CollectAll(std::move(input));
    CHECK_EXECUTOR(&e3);

    auto out = co_await std::move(combined_lazy);
    EXPECT_EQ(2u, out.size());
    CHECK_EXECUTOR(&e3);
  };
  SyncAwait(test3().Via(&e3));
}

TEST_F(LazyTest, TestCollectAllBatched) {
  int task_num = 10;
  long total = 0;
  for (auto i = 0; i < task_num; i++) {
    total += i;
  }

  executors::SimpleExecutor e1(10);
  executors::SimpleExecutor e2(10);
  executors::SimpleExecutor e3(10);
  executors::SimpleExecutor e4(10);
  executors::SimpleExecutor e5(10);
  executors::SimpleExecutor e6(10);
  // Lazy:
  // CollectAllWindowed, maxConcurrency is task_num;
  auto test1 = [this, &e1, &task_num]() -> Lazy<int> {
    std::vector<Lazy<int>>
        input;
    for (auto i = 0; i < task_num; i++) {
      input.push_back(GetValue(i));
    }
    CHECK_EXECUTOR(&e1);
    auto combined_lazy = CollectAllWindowed(task_num, false, std::move(input));
    CHECK_EXECUTOR(&e1);

    auto out = co_await std::move(combined_lazy);

    CHECK_EXECUTOR(&e1);
    EXPECT_EQ(task_num, out.size());
    co_await CurrentExecutor();
    int sum = 0;
    for (auto &i : out) {
      sum += i.Value();
    }
    co_return sum;
  };

  {
    ScopedBench tt{"Lazy: CollectAll_maxConcurrency_is_task_num", 1};
    EXPECT_EQ(total, SyncAwait(test1().Via(&e1)));
  }

  auto test1_void = [this, &e1, &task_num]() -> Lazy<> {
    std::vector<Lazy<>> input;
    for (auto i = 0; i < 10; i++) {
      input.push_back(MakeVoidTask());
    }
    CHECK_EXECUTOR(&e1);
    auto combinedLazy = CollectAllWindowed(task_num, false, std::move(input));
    CHECK_EXECUTOR(&e1);

    auto out = co_await std::move(combinedLazy);

    CHECK_EXECUTOR(&e1);
    co_await CurrentExecutor();
  };
  SyncAwait(test1_void().Via(&e1));

  // Lazy:
  // CollectAllWindowed, maxConcurrency is 10;
  auto test2 = [this, &e2, &task_num]() -> Lazy<int> {
    std::vector<Lazy<int>> input;
    for (auto i = 0; i < task_num; i++) {
      input.push_back(GetValue(i));
    }
    CHECK_EXECUTOR(&e2);
    auto combinedLazy = CollectAllWindowed(10, false, std::move(input));
    CHECK_EXECUTOR(&e2);

    auto out = co_await std::move(combinedLazy);

    CHECK_EXECUTOR(&e2);
    EXPECT_EQ(task_num, out.size());
    co_await CurrentExecutor();
    int sum = 0;
    for (auto &i : out) {
      sum += i.Value();
    }
    co_return sum;
  };
  {
    ScopedBench tt{"Lazy: CollectAll_maxConcurrency_is_10", 1};
    EXPECT_EQ(total, SyncAwait(test2().Via(&e2)));
  }

  auto test2_void = [this, &e2, &task_num]() -> Lazy<> {
    std::vector<Lazy<>> input;
    for (auto i = 0; i < task_num; i++) {
      input.push_back(MakeVoidTask());
    }
    CHECK_EXECUTOR(&e2);
    auto combinedLazy = CollectAllWindowed(10, false, std::move(input));
    CHECK_EXECUTOR(&e2);

    auto out = co_await std::move(combinedLazy);

    EXPECT_EQ(task_num, out.size());
    CHECK_EXECUTOR(&e2);
    co_await CurrentExecutor();
  };
  SyncAwait(test2_void().Via(&e2));

  // Lazy:
  // CollectAllWindowed, maxConcurrency is 10
  // inAlloc
  std::allocator<Lazy<int>> inAlloc;
  std::allocator<Try<int>> outAlloc;
  auto test3 = [this, &e3, &inAlloc, &task_num]() -> Lazy<int> {
    std::vector<Lazy<int>, std::allocator<Lazy<int>>> input(inAlloc);
    for (auto i = 0; i < task_num; i++) {
      input.push_back(GetValue(i));
    }
    CHECK_EXECUTOR(&e3);
    auto combinedLazy = CollectAllWindowed(10, false, std::move(input));
    CHECK_EXECUTOR(&e3);
    auto out = co_await std::move(combinedLazy);

    CHECK_EXECUTOR(&e3);
    EXPECT_EQ(task_num, out.size());
    int sum = 0;
    for (auto &i : out) {
      sum += i.Value();
    }
    co_return sum;
  };
  {
    ScopedBench tt{"Lazy: CollectAll_maxConcurrency_is_10_inAlloc", 1};
    EXPECT_EQ(total, SyncAwait(test3().Via(&e3)));
  }

  std::allocator<Lazy<>> inAllocVoid;
  auto test3_void = [this, &e3, &inAllocVoid, &task_num]() -> Lazy<> {
    std::vector<Lazy<>, std::allocator<Lazy<>>> input(inAllocVoid);
    for (auto i = 0; i < task_num; i++) {
      input.push_back(MakeVoidTask());
    }
    CHECK_EXECUTOR(&e3);
    auto combinedLazy = CollectAllWindowed(10, false, std::move(input));
    CHECK_EXECUTOR(&e3);
    auto out = co_await std::move(combinedLazy);

    EXPECT_EQ(task_num, out.size());
    CHECK_EXECUTOR(&e3);
  };
  SyncAwait(test3_void().Via(&e3));

  // Lazy:
  // CollectAllWindowed, maxConcurrency is 10
  // inAlloc && outAlloc
  auto test4 = [this, &e4, &inAlloc, &outAlloc, &task_num]() -> Lazy<int> {
    std::vector<Lazy<int>, std::allocator<Lazy<int>>> input(inAlloc);
    for (auto i = 0; i < task_num; i++) {
      input.push_back(GetValue(i));
    }
    CHECK_EXECUTOR(&e4);
    auto combinedLazy = CollectAllWindowed(10, false, std::move(input), outAlloc);
    CHECK_EXECUTOR(&e4);
    auto out = co_await std::move(combinedLazy);
    EXPECT_EQ(task_num, out.size());
    CHECK_EXECUTOR(&e4);
    int sum = 0;
    for (auto &i : out) {
      sum += i.Value();
    }
    co_return sum;
  };
  {
    ScopedBench tt{"Lazy: CollectAll_maxConcurrency_is_10_inAlloc_outAlloc", 1};
    EXPECT_EQ(total, SyncAwait(test4().Via(&e4)));
  }

  // RescheduleLazy:
  // CollectAllWindowed, maxConcurrency is task_num;
  auto test5 = [this, &e4, &e5, &e6, &task_num]() -> Lazy<int> {
    std::vector<RescheduleLazy<int>> input;
    for (auto i = 0; i < task_num; i++) {
      if (i % 2) {
        input.push_back(GetValue(i).Via(&e4));
      } else {
        input.push_back(GetValue(i).Via(&e5));
      }
    }

    CHECK_EXECUTOR(&e6);
    auto combinedLazy = CollectAllWindowed(task_num, false, std::move(input));
    CHECK_EXECUTOR(&e6);

    auto out = co_await std::move(combinedLazy);

    CHECK_EXECUTOR(&e6);
    EXPECT_EQ(task_num, out.size());
    int sum = 0;
    for (auto &i : out) {
      sum += i.Value();
    }
    co_return sum;
  };
  {
    ScopedBench tt{"RescheduleLazy: CollectAll_maxConcurrency_is_task_num", 1};
    EXPECT_EQ(total, SyncAwait(test5().Via(&e6)));
  }

  auto test5_void = [this, &e4, &e5, &e6, &task_num]() -> Lazy<> {
    std::vector<RescheduleLazy<>> input;
    for (auto i = 0; i < task_num; i++) {
      if (i % 2) {
        input.push_back(MakeVoidTask().Via(&e4));
      } else {
        input.push_back(MakeVoidTask().Via(&e5));
      }
    }

    CHECK_EXECUTOR(&e6);
    auto combinedLazy = CollectAllWindowed(task_num, false, std::move(input));
    CHECK_EXECUTOR(&e6);

    auto out = co_await std::move(combinedLazy);

    EXPECT_EQ(task_num, out.size());
    CHECK_EXECUTOR(&e6);
  };
  SyncAwait(test5_void().Via(&e6));

  // RescheduleLazy:
  // CollectAllWindowed, maxConcurrency is 10;
  auto test6 = [this, &e4, &e5, &e6, &task_num]() -> Lazy<int> {
    std::vector<RescheduleLazy < int>>
    input;
    for (auto i = 0; i < task_num; i++) {
      if (i % 2) {
        input.push_back(GetValue(i).Via(&e4));
      } else {
        input.push_back(GetValue(i).Via(&e5));
      }
    }

    CHECK_EXECUTOR(&e6);
    auto combinedLazy = CollectAllWindowed(10, false, std::move(input));
    CHECK_EXECUTOR(&e6);

    auto out = co_await std::move(combinedLazy);

    CHECK_EXECUTOR(&e6);
    EXPECT_EQ(task_num, out.size());
    int sum = 0;
    for (auto &i : out) {
      sum += i.Value();
    }
    co_return sum;
  };
  {
    ScopedBench tt{"RescheduleLazy: CollectAll_maxConcurrency_is_10", 1};
    EXPECT_EQ(total, SyncAwait(test6().Via(&e6)));
  }

  auto test6_void = [this, &e4, &e5, &e6, &task_num]() -> Lazy<> {
    std::vector<RescheduleLazy<>> input;
    for (auto i = 0; i < task_num; i++) {
      if (i % 2) {
        input.push_back(MakeVoidTask().Via(&e4));
      } else {
        input.push_back(MakeVoidTask().Via(&e5));
      }
    }

    CHECK_EXECUTOR(&e6);
    auto combinedLazy = CollectAllWindowed(10, false, std::move(input));
    CHECK_EXECUTOR(&e6);
    auto out = co_await std::move(combinedLazy);
    EXPECT_EQ(task_num, out.size());
    CHECK_EXECUTOR(&e6);
  };
  SyncAwait(test6_void().Via(&e6));

  // RescheduleLazy:
  // CollectAllWindowed, maxConcurrency is 10;
  // inAlloc1
  std::allocator<RescheduleLazy<int>> inAlloc1;
  std::allocator<Try<int>> outAlloc1;
  auto test7 = [this, &e4, &e5, &e6, &inAlloc1, &task_num]() -> Lazy<int> {
    std::vector<RescheduleLazy<int>, std::allocator<RescheduleLazy<int>>>
        input(inAlloc1);
    for (auto i = 0; i < task_num; i++) {
      if (i % 2) {
        input.push_back(GetValue(i).Via(&e4));
      } else {
        input.push_back(GetValue(i).Via(&e5));
      }
    }

    CHECK_EXECUTOR(&e6);
    auto combinedLazy = CollectAllWindowed(10, false, std::move(input));
    CHECK_EXECUTOR(&e6);

    auto out = co_await std::move(combinedLazy);

    CHECK_EXECUTOR(&e6);
    EXPECT_EQ(task_num, out.size());
    int sum = 0;
    for (size_t i = 0; i < out.size(); i++) {
      sum += out[i].Value();
    }
    co_return sum;
  };
  {
    ScopedBench tt{"RescheduleLazy: CollectAll_maxConcurrency_is_10_inAlloc", 1};
    EXPECT_EQ(total, SyncAwait(test7().Via(&e6)));
  }

  std::allocator<RescheduleLazy<>> inAlloc1Void;
  auto test7_void = [this, &e4, &e5, &e6, &inAlloc1Void,
      &task_num]() -> Lazy<> {
    std::vector<RescheduleLazy<>, std::allocator<RescheduleLazy<>>> input(
        inAlloc1Void);
    for (auto i = 0; i < task_num; i++) {
      if (i % 2) {
        input.push_back(MakeVoidTask().Via(&e4));
      } else {
        input.push_back(MakeVoidTask().Via(&e5));
      }
    }

    CHECK_EXECUTOR(&e6);
    auto combinedLazy = CollectAllWindowed(10, false, std::move(input));
    CHECK_EXECUTOR(&e6);
    auto out = co_await std::move(combinedLazy);

    EXPECT_EQ(task_num, out.size());
    CHECK_EXECUTOR(&e6);
  };
  SyncAwait(test7_void().Via(&e6));

  // RescheduleLazy:
  // CollectAllWindowed, maxConcurrency is 10;
  // inAlloc1, outAlloc1
  auto test8 = [this, &e4, &e5, &e6, inAlloc1, outAlloc1,
      &task_num]() -> Lazy<int> {
    std::vector<RescheduleLazy<int>, std::allocator<RescheduleLazy<int>>>
        input(inAlloc1);
    for (auto i = 0; i < task_num; i++) {
      if (i % 2) {
        input.push_back(GetValue(i).Via(&e4));
      } else {
        input.push_back(GetValue(i).Via(&e5));
      }
    }

    CHECK_EXECUTOR(&e6);
    auto combinedLazy = CollectAllWindowed(10, false, std::move(input), outAlloc1);
    CHECK_EXECUTOR(&e6);

    auto out = co_await std::move(combinedLazy);

    CHECK_EXECUTOR(&e6);
    EXPECT_EQ(task_num, out.size());
    int sum = 0;
    for (size_t i = 0; i < out.size(); i++) {
      sum += out[i].Value();
    }
    co_return sum;
  };
  {
    ScopedBench tt{"RescheduleLazy: CollectAll_maxConcurrency_is_10_inAlloc_outAlloc", 1};
    EXPECT_EQ(total, SyncAwait(test8().Via(&e6)));
  }
}

TEST_F(LazyTest, TestCollectAllWithAllocator) {
  executors::SimpleExecutor e1(5);
  executors::SimpleExecutor e2(5);
  executors::SimpleExecutor e3(5);
  std::allocator<Lazy<int>> inAlloc;
  std::allocator<Try<int>> outAlloc;
  auto test0 = [this, &e1, &inAlloc]() -> Lazy<int> {
    std::vector<Lazy<int>, std::allocator<Lazy<int>>> input(inAlloc);
    input.push_back(GetValue(1));
    input.push_back(GetValue(2));
    CHECK_EXECUTOR(&e1);
    auto combinedLazy = CollectAll(std::move(input));
    CHECK_EXECUTOR(&e1);

    auto out = co_await std::move(combinedLazy);

    CHECK_EXECUTOR(&e1);
    EXPECT_EQ(2u, out.size());
    co_return out[0].Value() + out[1].Value();
  };
  EXPECT_EQ(3, SyncAwait(test0().Via(&e1)));

  // FIXME
  auto test1 = [this, &e1, &inAlloc, &outAlloc]() -> Lazy<int> {
    std::vector<Lazy<int>, std::allocator<Lazy<int>>> input(inAlloc);
    input.push_back(GetValue(1));
    input.push_back(GetValue(2));
    CHECK_EXECUTOR(&e1);
    auto combinedLazy = CollectAll(std::move(input), outAlloc);
    CHECK_EXECUTOR(&e1);

    auto out = co_await std::move(combinedLazy);

    CHECK_EXECUTOR(&e1);
    EXPECT_EQ(2u, out.size());
    co_return out[0].Value() + out[1].Value();
  };
  EXPECT_EQ(3, SyncAwait(test1().Via(&e1)));

  auto test2 = [this, &e1, &e2, &e3]() -> Lazy<int> {
    std::vector<RescheduleLazy<int>> input;
    input.push_back(GetValue(1).Via(&e1));
    input.push_back(GetValue(2).Via(&e2));

    CHECK_EXECUTOR(&e3);
    auto combinedLazy = CollectAll(std::move(input));
    CHECK_EXECUTOR(&e3);

    auto out = co_await std::move(combinedLazy);

    CHECK_EXECUTOR(&e3);
    EXPECT_EQ(2u, out.size());
    co_return out[0].Value() + out[1].Value();
  };
  EXPECT_EQ(3, SyncAwait(test2().Via(&e3)));
}

TEST_F(LazyTest, TestCollectAllVariadic) {
  // normal task
  executors::SimpleExecutor e1(5);
  auto test = [this, &e1]() -> Lazy<> {
    Lazy<int> intLazy = GetValue(2);
    Lazy<double> doubleLazy = GetValue(2.2);
    Lazy<std::string> stringLazy =
        GetValue(std::string("testCollectAllVariadic"));

    CHECK_EXECUTOR(&e1);
    auto combinedLazy = CollectAll(
        std::move(intLazy), std::move(doubleLazy), std::move(stringLazy));
    CHECK_EXECUTOR(&e1);

    auto out_tuple = co_await std::move(combinedLazy);
    EXPECT_EQ(3u, std::tuple_size<decltype(out_tuple)>::value);

    auto v_try_int = std::get<0>(std::move(out_tuple));
    auto v_try_double = std::get<1>(std::move(out_tuple));
    auto v_try_string = std::get<2>(std::move(out_tuple));

    EXPECT_EQ(2, v_try_int.Value());
    EXPECT_DOUBLE_EQ(2.2, v_try_double.Value());
    EXPECT_STREQ("testCollectAllVariadic", v_try_string.Value().c_str());

    CHECK_EXECUTOR(&e1);
  };
  SyncAwait(test().Via(&e1));

  // void task
  executors::SimpleExecutor e2(5);
  auto test1 = [this, &e2]() -> Lazy<> {
    Lazy<int> intLazy = GetValue(2);
    Lazy<void> voidLazy01 = MakeVoidTask();
    Lazy<void> voidLazy02 = TestException();

    CHECK_EXECUTOR(&e2);
    auto combinedLazy = CollectAll(
        std::move(intLazy), std::move(voidLazy01), std::move(voidLazy02));
    CHECK_EXECUTOR(&e2);

    auto out_tuple = co_await std::move(combinedLazy);
    EXPECT_EQ(3u, std::tuple_size<decltype(out_tuple)>::value);

    auto v_try_int = std::get<0>(std::move(out_tuple));
    auto v_try_void01 = std::get<1>(std::move(out_tuple));
    auto v_try_void02 = std::get<2>(std::move(out_tuple));

    EXPECT_EQ(2u, v_try_int.Value());

    bool b1 = std::is_same_v<Try<void>, decltype(v_try_void01)>;
    bool b2 = std::is_same_v<Try<void>, decltype(v_try_void02)>;
    try {  // v_try_void02 throw exception
      EXPECT_TRUE(true);
      v_try_void02.Value();
      EXPECT_TRUE(false);
    } catch (const std::runtime_error& e) {
      EXPECT_TRUE(true);
    }
    EXPECT_TRUE(b1);
    EXPECT_TRUE(b2);

    CHECK_EXECUTOR(&e2);
  };
  SyncAwait(test1().Via(&e2));

  // RescheduleLazy
  executors::SimpleExecutor e3(5);
  executors::SimpleExecutor e4(5);
  auto test2 = [this, &e1, &e2, &e3, &e4]() -> Lazy<> {
    RescheduleLazy<int> intLazy = GetValue(2).Via(&e2);
    RescheduleLazy<double> doubleLazy = GetValue(2.2).Via(&e3);
    RescheduleLazy<std::string> stringLazy =
        GetValue(std::string("testCollectAllVariadic")).Via(&e4);

    CHECK_EXECUTOR(&e1);
    auto combinedLazy = CollectAll(
        std::move(intLazy), std::move(doubleLazy), std::move(stringLazy));
    CHECK_EXECUTOR(&e1);

    auto out_tuple = co_await std::move(combinedLazy);
    EXPECT_EQ(3u, std::tuple_size<decltype(out_tuple)>::value);

    auto v_try_int = std::get<0>(std::move(out_tuple));
    auto v_try_double = std::get<1>(std::move(out_tuple));
    auto v_try_string = std::get<2>(std::move(out_tuple));

    EXPECT_EQ(2, v_try_int.Value());
    EXPECT_DOUBLE_EQ(2.2, v_try_double.Value());
    EXPECT_STREQ("testCollectAllVariadic", v_try_string.Value().c_str());

    CHECK_EXECUTOR(&e1);
  };
  SyncAwait(test2().Via(&e1));

  // void RescheduleLazy
  auto test3 = [this, &e1, &e2, &e3, &e4]() -> Lazy<> {
    RescheduleLazy<int> intLazy = GetValue(2).Via(&e2);
    RescheduleLazy<void> voidLazy01 = MakeVoidTask().Via(&e3);
    RescheduleLazy<void> voidLazy02 = MakeVoidTask().Via(&e4);

    CHECK_EXECUTOR(&e1);
    auto combinedLazy = CollectAll(
        std::move(intLazy), std::move(voidLazy01), std::move(voidLazy02));
    CHECK_EXECUTOR(&e1);

    auto out_tuple = co_await std::move(combinedLazy);
    EXPECT_EQ(3u, std::tuple_size<decltype(out_tuple)>::value);

    auto v_try_int = std::get<0>(std::move(out_tuple));
    auto v_try_void01 = std::get<1>(std::move(out_tuple));
    auto v_try_void02 = std::get<2>(std::move(out_tuple));

    EXPECT_EQ(2u, v_try_int.Value());
    bool b1 = std::is_same_v<Try<void>, decltype(v_try_void01)>;
    bool b2 = std::is_same_v<Try<void>, decltype(v_try_void02)>;
    EXPECT_TRUE(b1);
    EXPECT_TRUE(b2);

    CHECK_EXECUTOR(&e1);
  };
  SyncAwait(test3().Via(&e1));
}

TEST_F(LazyTest, TestCollectAny) {
  srand((unsigned)time(NULL));
  executors::SimpleExecutor e1(10);
  executors::SimpleExecutor e2(10);
  executors::SimpleExecutor e3(10);

  auto test = [this]() -> Lazy<int> {
    std::vector<Lazy<int>> input;
    input.push_back(GetValueWithSleep(1));
    input.push_back(GetValueWithSleep(2));
    input.push_back(GetValueWithSleep(2));
    input.push_back(GetValueWithSleep(3));
    input.push_back(GetValueWithSleep(4));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(3));
    input.push_back(GetValueWithSleep(4));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(5));
    input.push_back(GetValueWithSleep(5));
    auto combinedLazy = CollectAny(std::move(input));
    auto out = co_await std::move(combinedLazy);
    EXPECT_GT(out.value.Value(), 0);
    EXPECT_GE(out.index, 0);
    co_return out.value.Value();
  };
  ASSERT_GT(SyncAwait(test().Via(&e1)), 0);

  auto test2 = [this, &e1, &e2, &e3]() -> Lazy<int> {
    std::vector<RescheduleLazy<int>> input;
    input.push_back(GetValueWithSleep(11).Via(&e1));
    input.push_back(GetValueWithSleep(12).Via(&e1));
    input.push_back(GetValueWithSleep(13).Via(&e1));
    input.push_back(GetValueWithSleep(14).Via(&e1));
    input.push_back(GetValueWithSleep(15).Via(&e1));

    input.push_back(GetValueWithSleep(25).Via(&e2));
    input.push_back(GetValueWithSleep(21).Via(&e2));
    input.push_back(GetValueWithSleep(22).Via(&e2));
    input.push_back(GetValueWithSleep(23).Via(&e2));
    input.push_back(GetValueWithSleep(24).Via(&e2));
    input.push_back(GetValueWithSleep(25).Via(&e2));

    CHECK_EXECUTOR(&e3);
    auto combinedLazy = CollectAny(std::move(input));
    CHECK_EXECUTOR(&e3);

    auto out = co_await std::move(combinedLazy);

    EXPECT_GT(out.value.Value(), 10);
    EXPECT_GE(out.index, 0);
    co_return out.value.Value();
  };
  ASSERT_GT(SyncAwait(test2().Via(&e3)), 10);

  std::this_thread::sleep_for(std::chrono::seconds(2));
}

TEST_F(LazyTest, TestException) {
  executors::SimpleExecutor e1(1);
  int ret = 0;
  auto test0 = [&]() -> Lazy<> {
    throw std::runtime_error("error test0");
    co_return;
  };
  auto test1 = [&]() -> Lazy<int> {
    throw std::runtime_error("error test1");
    co_return 0;
  };

  auto test2 = [&]() mutable -> Lazy<> {
    try {
      co_await test0();
      ret += 1;
    } catch (const std::runtime_error& e) {
      ;
    }
    try {
      co_await test1();
      ret += 1;
    } catch (const std::runtime_error& e) {
      ;
    }
  };
  SyncAwait(test2().Via(&e1));
  EXPECT_EQ(0, ret);
}

TEST_F(LazyTest, TestContext) {
  executors::SimpleExecutor e1(10);
  executors::SimpleExecutor e2(10);

  auto addTwo = [&](int x) -> Lazy<int> {
    CHECK_EXECUTOR(&e2);
    auto tid = std::this_thread::get_id();
    auto tmp = co_await GetValue(x);
    CHECK_EXECUTOR(&e2);
    EXPECT_EQ(tid, std::this_thread::get_id());
    co_return tmp + 2;
  };
  {
    auto test = [&, this]() -> Lazy<int> {
      CHECK_EXECUTOR(&e1);
      auto tid = std::this_thread::get_id();
      int y = co_await PlusOne();
      CHECK_EXECUTOR(&e1);
      EXPECT_EQ(tid, std::this_thread::get_id());
      int z = co_await addTwo(y).Via(&e2);
      CHECK_EXECUTOR(&e1);
      EXPECT_EQ(tid, std::this_thread::get_id());
      co_return z;
    };
    TriggerValue(100);
    auto val = SyncAwait(test().Via(&e1));
    EXPECT_EQ(103, val);
  }
}

struct A {
  int* a;
  A(int x) : a(new int(x)) {}
  ~A() {
    if (a) {
      destroyOrder.push_back(*a);
      delete a;
    }
  }
  int show() { return *a; }
  A(A&& other) : a(other.a) { other.a = nullptr; }

  static std::vector<int> destroyOrder;
};

std::vector<int> A::destroyOrder;

Lazy<int> getValue(A x) {
  struct ValueAwaiter {
    int value;
    ValueAwaiter(int v) : value(v) {}

    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> continuation) noexcept {
      std::thread([c = std::move(continuation)]() mutable {
        c.resume();
      }).detach();
    }
    int await_resume() noexcept { return value; }
  };
  auto z = co_await ValueAwaiter(x.show());

  co_return z + x.show();
}

Lazy<int> f1(A a) {
  auto v = co_await getValue(1);
  std::vector<Lazy<int>> lzs;
  lzs.push_back(getValue(7));
  auto r = co_await CollectAll(std::move(lzs));
  co_return a.show() + v + r[0].Value();
};

Lazy<int> f0(A a) {
  int v = 0;
  v = co_await f1(1);
  std::cout << a.show() << std::endl;
  co_return v;
}

TEST_F(LazyTest, TestDestroyOrder) {
  A::destroyOrder.clear();
  auto test = []() -> Lazy<int> {
    auto a = new A(999);
    auto l = f0(0);
    auto v = co_await l;
    delete a;
    co_return v;
  };
  SyncAwait(test().Via(&executor_));
  EXPECT_THAT(A::destroyOrder, testing::ElementsAre(1, 7, 1, 0, 999));
}

namespace detail {
template <int N>
Lazy<int> lazy_fn() {
  co_return N + co_await lazy_fn<N - 1>();
}

template <>
Lazy<int> lazy_fn<0>() {
  co_return 0;
}
}  // namespace detail
TEST_F(LazyTest, TestLazyPerf) {
  auto test_loop = 10;
  auto total = 0;

  auto one = [](int n) -> Lazy<int> {
    auto x = n;
    co_return x;
  };

  auto loop_starter = [&]() -> Lazy<> {
    ScopedBench scoper("lazy 30 loop call", test_loop);
    for (auto k = 0; k < test_loop; ++k) {
      for (auto i = 1; i <= 30; ++i) {
        total += co_await one(i);
      }
    }
  };
  SyncAwait(loop_starter());
  EXPECT_EQ(total, 4650);

  total = 0;
  auto chain_starter = [&]() -> Lazy<> {
    ScopedBench scoper("lazy 30 chain call", test_loop);
    for (auto i = 0; i < test_loop; ++i) {
      total += co_await detail::lazy_fn<30>();
    }
  };
  SyncAwait(chain_starter());
  EXPECT_EQ(total, 4650);
}

TEST_F(LazyTest, TestCollectAllParallel) {
  executors::SimpleExecutor e1(10);
  auto test1 = [this]() -> Lazy<> {
    std::vector<Lazy<std::thread::id>> input;
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    auto combinedLazy = CollectAll(std::move(input));
    auto out = co_await std::move(combinedLazy);
    EXPECT_EQ(out[0].Value(), out[1].Value());
    EXPECT_EQ(out[0].Value(), out[2].Value());
    EXPECT_EQ(out[0].Value(), out[3].Value());
    EXPECT_EQ(out[0].Value(), out[4].Value());
    EXPECT_EQ(out[0].Value(), out[5].Value());
  };
  SyncAwait(test1().Via(&e1));

  auto test2 = [this]() -> Lazy<> {
    std::vector<Lazy<std::thread::id>> input;
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    auto combinedLazy = CollectAllPara(std::move(input));
    auto out = co_await std::move(combinedLazy);
    std::cout << out[0].Value() << std::endl;
    std::cout << out[1].Value() << std::endl;
    std::cout << out[2].Value() << std::endl;
    std::cout << out[3].Value() << std::endl;
    std::cout << out[4].Value() << std::endl;
    std::cout << out[5].Value() << std::endl;
    std::cout << out[6].Value() << std::endl;
    std::cout << out[7].Value() << std::endl;
    std::set<std::thread::id> ss;
    ss.insert(out[0].Value());
    ss.insert(out[1].Value());
    ss.insert(out[2].Value());
    ss.insert(out[3].Value());
    ss.insert(out[4].Value());
    ss.insert(out[5].Value());
    ss.insert(out[6].Value());
    ss.insert(out[7].Value());
    // FIXME: input tasks maybe run not in different thread.
    EXPECT_GT(ss.size(), 2);
  };
  SyncAwait(test2().Via(&e1));
}

std::vector<int> result;
Lazy<void> makeTest(int value) {
  std::this_thread::sleep_for(1000us);
  result.push_back(value);
  co_return;
}

TEST_F(LazyTest, testBatchedCollectAll) {
  executors::SimpleExecutor e1(10);
  auto test1 = [this]() -> Lazy<> {
    std::vector<Lazy<std::thread::id>> input;
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    input.push_back(GetThreadId());
    auto combinedLazy = CollectAllWindowed(
        2 /*maxConcurrency*/, false /*yield*/, std::move(input));
    auto out = co_await std::move(combinedLazy);
    std::cout
        << "input tasks maybe run not in different thread, thread id: "
        << std::endl;
    std::cout << out[0].Value() << std::endl;
    std::cout << out[1].Value() << std::endl;
    std::cout << out[2].Value() << std::endl;
    std::cout << out[3].Value() << std::endl;
    std::cout << out[4].Value() << std::endl;
    std::cout << out[5].Value() << std::endl;
    std::cout << out[6].Value() << std::endl;
    std::set<std::thread::id> ss;
    ss.insert(out[0].Value());
    ss.insert(out[1].Value());
    ss.insert(out[2].Value());
    ss.insert(out[3].Value());
    ss.insert(out[4].Value());
    ss.insert(out[5].Value());
    ss.insert(out[6].Value());
    // FIXME: input tasks maybe run not in different thread.
    EXPECT_GE(ss.size(), 1);
  };
  SyncAwait(test1().Via(&e1));

  // test Yield
  executors::SimpleExecutor e2(1);
  int value1 = 1;
  int value2 = 2;
  int value3 = 3;
  int value4 = 4;

  int value5 = 5;
  int value6 = 6;
  int value7 = 7;
  int value8 = 8;

  std::vector<Lazy<void>> input1;
  input1.push_back(makeTest(value1));
  input1.push_back(makeTest(value2));
  input1.push_back(makeTest(value3));
  input1.push_back(makeTest(value4));

  std::vector<Lazy<void>> input2;
  input2.push_back(makeTest(value5));
  input2.push_back(makeTest(value6));
  input2.push_back(makeTest(value7));
  input2.push_back(makeTest(value8));

  CollectAllWindowed(1, true, std::move(input1))
      .Via(&e2)
      .Start([](auto result) {});
  CollectAllWindowed(1, true, std::move(input2))
      .Via(&e2)
      .Start([](auto result) {});
  std::this_thread::sleep_for(500000us);
  std::vector<int> expect{1, 5, 2, 6, 3, 7, 4, 8};
  for (size_t i = 0; i < result.size(); ++i) {
    EXPECT_EQ(expect[i], result[i]);
    std::cout << "expect[" << i << "]: " << expect[i] << ", "
              << "result[" << i << "]: " << result[i] << std::endl;
  }
}

TEST_F(LazyTest, TestDetach) {
  std::binary_semaphore sem(0);
  int count = 0;
  executors::SimpleExecutor e1(1);
  auto test1 = [&]() -> Lazy<int> {
    count += 2;
    sem.release();
    co_return count;
  };
  test1().Via(&e1).Detach();
  sem.acquire();
  EXPECT_EQ(count, 2);
}

} // namespace async_simple::coro
