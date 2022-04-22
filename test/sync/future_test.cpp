#include <async_simple/sync/future.hpp>

#include "async_simple_test.hpp"

#include <async_simple/executor/simple_executor.hpp>
#include <async_simple/sync/future_helper.hpp>

using namespace async_simple::executors;
using namespace std::chrono_literals;

namespace async_simple {

namespace {

enum DummyState : int {
  CONSTRUCTED = 1,
  DESTRUCTED = 2,
};

struct Dummy {
  Dummy() : state(nullptr), value(0) {}
  Dummy(int x) : state(nullptr), value(x) {}
  Dummy(int *state_) : state(state_), value(0) {
    if (state) {
      (*state) |= CONSTRUCTED;
    }
  }
  Dummy(Dummy &&other) : state(other.state), value(other.value) {
    other.state = nullptr;
  }
  Dummy &operator=(Dummy &&other) {
    if (this != &other) {
      std::swap(other.state, state);
      std::swap(other.value, value);
    }
    return *this;
  }

  ~Dummy() {
    if (state) {
      (*state) |= DESTRUCTED;
      state = nullptr;
    }
  }

  Dummy(const Dummy &) = delete;
  Dummy &operator=(const Dummy &) = delete;

  int *state = nullptr;
  int value = 0;

  Dummy &operator+(const int &rhs) &{
    value += rhs;
    return *this;
  }
  Dummy &&operator+(const int &rhs) &&{
    value += rhs;
    return std::move(*this);
  }
  Dummy &operator+(const Dummy &rhs) &{
    value += rhs.value;
    return *this;
  }
  Dummy &&operator+(const Dummy &rhs) &&{
    value += rhs.value;
    return std::move(*this);
  }

  bool operator==(const Dummy &other) const { return value == other.value; }
};

} // anonymouse namespace

class FutureTest : public testing::Test {
 public:
  template<typename T>
  void DoTestType(bool ready_future) {
    SimpleExecutor executor(5);
    Promise<T> p;
    std::vector<int> order;
    std::mutex mtx;
    auto record = [&order, &mtx](int x) {
      std::lock_guard l(mtx);
      order.push_back(x);
    };
    auto future = p.GetFuture().Via(&executor);
    if (ready_future) {
      future = MakeReadyFuture(T(1000));
    }
    auto f = std::move(future)
        .ThenTry([record](Try<T> &&t) {
          record(0);
          return std::move(t).Value() + 100;
        })
        .ThenTry([&executor, record](Try<T> t) mutable {
          record(1);
          Promise<T> p;
          auto f = p.GetFuture().Via(&executor);
          p.SetValue(std::move(t).Value() + 10);
          return f;
        })
        .ThenValue([record](T &&x) {
          record(2);
          return std::move(x) + 1;
        });
    p.SetValue(T(1000));
    f.Wait();
    EXPECT_EQ(order.size(), 3u);
    int last = -1;
    for (auto a : order) {
      EXPECT_LT(last, a);
      last = a;
    }
    EXPECT_EQ(T(1111), std::move(f).Get());
  }
};

TEST_F(FutureTest, TestSimpleProcess) {
  SimpleExecutor executor(5);

  Promise<int> p;
  auto future = p.GetFuture();
  EXPECT_TRUE(p.Valid());

  int output = 0;
  auto f = std::move(future)
      .Via(&executor)
      .ThenTry([&output](Try<int> &&t) {
        output = t.Value();
        return 123;
      });
  p.SetValue(456);

  f.Wait();
  const Try<int> &result = f.Result();
  EXPECT_TRUE(result.Available());
  EXPECT_FALSE(result.HasException());

  EXPECT_EQ(std::move(f).Get(), 123);
  EXPECT_EQ(output, 456);
}

TEST_F(FutureTest, TestGetSet) {
  Promise<int> p;
  auto f = p.GetFuture();
  ASSERT_THROW(p.GetFuture(), std::logic_error);
  p.SetValue(456);
  f.Wait();
  EXPECT_EQ(f.Value(), 456);
}

TEST_F(FutureTest, TestThenValue) {
  SimpleExecutor executor(5);

  Promise<int> p;
  auto future = p.GetFuture();
  EXPECT_TRUE(p.Valid());

  int output = 0;
  auto f = std::move(future)
      .Via(&executor)
      .ThenValue([&output](int64_t t) {
        output = t;
        return 123;
      });
  p.SetValue(456);
  f.Wait();
  const Try<int> &t = f.Result();
  EXPECT_TRUE(t.Available());
  EXPECT_FALSE(t.HasException());
  EXPECT_EQ(std::move(f).Get(), 123);
  EXPECT_EQ(output, 456);
}

TEST_F(FutureTest, TestChainedFuture) {
  SimpleExecutor executor(5);
  Promise<int> p;
  int output0 = 0;
  int output1 = 0;
  int output2 = 0;
  std::vector<int> order;
  std::mutex mtx;
  auto record = [&order, &mtx](int x) {
    std::lock_guard l(mtx);
    order.push_back(x);
  };
  auto future = p.GetFuture().Via(&executor);
  auto f = std::move(future)
      .ThenTry([&output0, record](Try<int> &&t) {
        record(0);
        output0 = t.Value();
        return t.Value() + 100;
      })
      .ThenTry([&output1, &executor, record](Try<int> &&t) {
        record(1);
        output1 = t.Value();
        Promise<int> p;
        auto f = p.GetFuture().Via(&executor);
        p.SetValue(t.Value() + 10);
        return f;
      })
      .ThenValue([&output2, record](int x) {
        record(2);
        output2 = x;
        return std::to_string(x);
      })
      .ThenValue([](std::string &&s) { return 1111.0; });
  p.SetValue(1000);
  f.Wait();
  EXPECT_EQ(order.size(), 3u);
  int last = -1;
  for (auto a : order) {
    EXPECT_LT(last, a);
    last = a;
  }
  EXPECT_EQ(output0, 1000);
  EXPECT_EQ(output1, 1100);
  EXPECT_EQ(output2, 1110);
  EXPECT_EQ(std::move(f).Get(), 1111.0);
}

TEST_F(FutureTest, TestClass) {
  DoTestType<int>(true);
  DoTestType<Dummy>(true);
  DoTestType<int>(false);
  DoTestType<Dummy>(false);
}

TEST_F(FutureTest, TestException) {
  SimpleExecutor executor(5);
  Promise<int> p;
  auto future = p.GetFuture().Via(&executor);
  EXPECT_TRUE(p.Valid());
  auto f = std::move(future)
      .ThenTry([](Try<int> x) { return x.Value(); })
      .ThenValue([](int x) { return x + 10; })
      .ThenTry([](Try<int> x) {
        try {
          return x.Value() + 1.0;
        } catch (...) {
          return -1.0;
        }
      });
  try {
    throw std::runtime_error("Failed");
  } catch (...) {
    p.SetException(std::current_exception());
  }
  f.Wait();
  const Try<double> &t = f.Result();
  EXPECT_TRUE(t.Available());
  EXPECT_FALSE(t.HasException());
  EXPECT_EQ(std::move(f).Get(), -1.0);
}

TEST_F(FutureTest, TestVoid) {
  SimpleExecutor executor(5);

  Promise<int> p;
  auto future = p.GetFuture().Via(&executor);
  EXPECT_TRUE(p.Valid());
  int output = 0;
  auto f = std::move(future)
      .ThenTry([&output](Try<int> x) { output = x.Value(); })
      .ThenTry([](auto &&) { return 200; });

  p.SetValue(100);
  f.Wait();
  EXPECT_EQ(std::move(f).Get(), 200);
  EXPECT_EQ(output, 100);
}

TEST_F(FutureTest, TestWait) {
  SimpleExecutor executor(5);
  int output;
  Promise<int> p;
  auto future = p.GetFuture().Via(&executor);
  EXPECT_TRUE(p.Valid());
  std::atomic<bool> begin_callback(false);
  std::atomic<int> done_callback(0);
  auto f = std::move(future)
      .ThenTry([&output, &begin_callback, &done_callback](Try<int> t) {
        int tmp = done_callback.load(std::memory_order_acquire);
        while (!done_callback.compare_exchange_weak(tmp, 1)) {
          tmp = done_callback.load(std::memory_order_release);
        }
        while (!begin_callback.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        output = t.Value();
        return output + 5;
      });
  auto t = std::thread([&p, &begin_callback, &done_callback]() {
    std::this_thread::sleep_for(100000us);
    EXPECT_EQ(0, done_callback.load(std::memory_order_acquire));
    p.SetValue(100);
    for (size_t i = 5; i > 0 && done_callback.load(std::memory_order_acquire) != 1; --i) {
      std::this_thread::sleep_for(1000us);
    }
    EXPECT_EQ(done_callback.load(std::memory_order_acquire), 1);
    begin_callback.store(true, std::memory_order_release);
    for (std::size_t i = 500; i > 0 && done_callback.load(std::memory_order_acquire) != 2; --i) {
      std::this_thread::sleep_for(1000us);
    }
    EXPECT_EQ(done_callback.load(std::memory_order_acquire), 2);
  });
  f.Wait();
  done_callback.store(2, std::memory_order_release);
  EXPECT_EQ(std::move(f).Get(), 105);
  EXPECT_EQ(output, 100);
  t.join();
}

TEST_F(FutureTest, TestWaitCallback) {
  SimpleExecutor executor(2), executor2(1);
  Promise<int> p;
  auto future = p.GetFuture().Via(&executor);
  EXPECT_TRUE(p.Valid());
  Promise<bool> p2;
  auto f = std::move(future)
      .ThenTry([&p2, &executor2](Try<int> &&t) {
        auto f = p2.GetFuture()
            .Via(&executor2)
            .ThenValue([x = std::move(t).Value()](bool y) {
              std::this_thread::sleep_for(10000us);
              return x;
            });
        return f;
      })
      .ThenValue([](int x) {
        std::this_thread::sleep_for(20ms);
        return std::make_pair(x + 1, x);
      })
      .ThenValue([&executor2](std::pair<int, int> &&v) {
        Promise<bool> p3;
        Future<int> f = p3.GetFuture()
            .Via(&executor2)
            .ThenValue([r = std::move(v)](bool y) {
              std::this_thread::sleep_for(30ms);
              return r.first * r.second;
            });
        p3.SetValue(true);
        return f;
      });
  p.SetValue(2);
  p2.SetValue(true);
  f.Wait();
  EXPECT_EQ(std::move(f).Get(), 6);
}

TEST_F(FutureTest, TestCollectAll) {
  SimpleExecutor executor(15);

  size_t n = 10;
  std::vector<Promise<Dummy>> promise(n);
  std::vector<Future<Dummy>> futures;
  for (size_t i = 0; i < n; ++i) {
    futures.push_back(promise[i].GetFuture().Via(&executor));
  }
  std::vector<int> expected;
  for (size_t i = 0; i < n; ++i) {
    expected.push_back(i);
  }
  auto f = CollectAll(futures.begin(), futures.end())
      .ThenValue([&expected](std::vector<Try<Dummy>> &&vec) {
        EXPECT_EQ(expected.size(), vec.size());
        for (size_t i = 0; i < vec.size(); ++i) {
          EXPECT_EQ(expected[i], vec[i].Value().value);
        }
        expected.clear();
      });

  for (size_t i = 0; i < n; ++i) {
    promise[i].SetValue(Dummy(i));
  }

  f.Wait();

  EXPECT_TRUE(expected.empty());
}

TEST_F(FutureTest, TestCollectReadyFutures) {
  SimpleExecutor executor(15);

  size_t n = 10;
  std::vector<Future<Dummy>> futures;
  for (size_t i = 0; i < n; ++i) {
    futures.push_back(MakeReadyFuture<Dummy>(i));
  }
  bool executed = false;
  auto f = CollectAll(futures.begin(), futures.end())
      .ThenValue([&executed, n](std::vector<Try<Dummy>> &&vec) {
        EXPECT_EQ(n, vec.size());
        for (size_t i = 0; i < vec.size(); ++i) {
          EXPECT_EQ(i, vec[i].Value().value);
        }
        executed = true;
      });
  f.Wait();
  EXPECT_TRUE(executed);
}

TEST_F(FutureTest, TestPromiseBroken) {
  Promise<Dummy> p;
  auto f = p.GetFuture();
  {
    // destruct p
    auto innerP = std::move(p);
    (void) innerP;
  }
  f.Wait();
  auto &r = f.Result();
  EXPECT_TRUE(r.Available());
  EXPECT_TRUE(r.HasException());
}

TEST_F(FutureTest, TestViaAfterWait) {
  Promise<int> promise;
  auto future = promise.GetFuture();

  auto t = std::thread([p = std::move(promise)]() mutable {
    std::this_thread::sleep_for(1s);
    p.SetValue(100);
  });

  future.Wait();
  std::move(future)
      .Via(nullptr)
      .ThenValue([](int v) mutable {
        ASSERT_EQ(100, v);
      });
  t.join();
}

TEST_F(FutureTest, TestReadyFuture) {
  auto future = MakeReadyFuture(3);
  future.Wait();
  std::move(future)
      .Via(nullptr)
      .ThenValue([](int v) mutable {
        ASSERT_EQ(3, v);
      });
}

TEST_F(FutureTest, TestPromiseCopy) {
  auto promise1 = std::make_unique<Promise<int>>();
  auto promise2 = std::make_unique<Promise<int>>();
  promise2->SetValue(0);
  auto future = promise1->GetFuture();
  *promise1 = *promise2;
  promise1.reset();
  ASSERT_THROW(future.Value(), std::runtime_error);
  auto promise3 = *promise2;
  promise2.reset();
  EXPECT_EQ(0, promise3.GetFuture().Value());
}

}

