#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_EXECUTOR_EXECUTOR_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_EXECUTOR_EXECUTOR_HPP_

#include "async_simple/executor/io_executor.hpp"

#include <chrono>
#include <coroutine>
#include <semaphore>
#include <thread>

namespace async_simple {

/// Executor的状态信息
struct ExecutorStat {
  size_t pending_task_count{0};
};

/// 一次调度的配置选项
struct ScheduleOptions {
  bool prompt{true};  ///< 是否应该立即调度
};

/// 获取当前Executor的Awaitable类型
/// ```
/// auto current_executor = co_await CurrentExecutor{};
/// ```
struct CurrentExecutor {};

/// Executor是一个调度器
/// Executor是协程调度的关键部分
/// 用户应该继承Executor类，并实现它的调度策略
class Executor : noncopyable {
 public:
  using Context = void *;
  static constexpr Context kNullContext = nullptr;

  using Duration = std::chrono::microseconds;
  using Func = std::function<void()>;

  class Awaitable;
  class Awaiter;
  class TimeAwaitable;
  class TimeAwaiter;

  Executor() = default;
  virtual ~Executor() = default;

  /// 调度一个函数的执行
  /// @return 当返回false时，表明func不会被调用；当返回true时，调度器需要确保函数会被执行
  virtual bool Schedule(Func func) = 0;

  /// 当前线程是否绑定到了这个Executor
  virtual bool CurrentThreadInExecutor() const {
    throw std::logic_error("Not implemented");
  }

  virtual ExecutorStat Stat() const {
    throw std::logic_error("Not implemented");
  }

  virtual size_t CurrentContextId() const { return 0; }

  /// 返回当前Context
  virtual Context Checkout() { return kNullContext; }

  /// 将函数调度都和之前一样的Context上
  virtual bool Checkin(Func func, Context context, ScheduleOptions options) {
    return Schedule(std::move(func));
  }
  /// 将函数调度都和之前一样的Context上
  virtual bool Checkin(Func func, Context context) {
    static ScheduleOptions options;
    return Checkin(std::move(func), context, options);
  }

  /// 在协程内部进行调度
  /// 使用方式: co_await executor.Schedule()
  Awaitable Schedule();

  /// 过一段时间后进行调度
  /// 使用方式: co_await executor.ScheduleAfter(sometime)
  TimeAwaitable ScheduleAfter(Duration duration);

  /// 获取IOExecutor, 如果不提供IOExecutor的话，则返回nullptr
  virtual IOExecutor *GetIOExecutor() {
    throw std::logic_error("Not implemented");
  }

  /// 阻塞当前线程，直到函数被调度
  /// @return 返回false表明调度失败，反之调度成功
  bool SyncSchedule(Func func) {
    std::binary_semaphore sem(0);
    auto ret = Schedule([f = std::move(func), &sem]() {
      f();
      sem.release();
    });
    if (!ret) return false; // 调度失败
    sem.acquire();
    return true;
  }

 private:
  /// 将一个虚函数声明为private的原因可参考：https://isocpp.org/wiki/faq/strange-inheritance#private-virtuals
  virtual void Schedule(Func func, Duration duration) {
    std::thread([this, f = std::move(func), duration] {
      std::this_thread::sleep_for(duration);
      Schedule(f);
    });
  }
};

/// 实现Executor::Schedule的Awaiter
class Executor::Awaiter {
 public:
  Awaiter(Executor *executor) : executor_(executor) {}

  bool await_ready() const noexcept {
    return executor_->CurrentThreadInExecutor();
  }

  template<typename PromiseType>
  void await_suspend(std::coroutine_handle<PromiseType> continuation) {
    auto func = [continuation]() mutable {
      continuation.resume();
    };
    executor_->Schedule(func);
  }

  void await_resume() const noexcept {}
 private:
  Executor *executor_;
};

class Executor::Awaitable {
 public:
  Awaitable(Executor *executor) : executor_(executor) {}

  auto CoAwait(Executor *) { return Awaiter(executor_); }
 private:
  Executor *executor_;
};

Executor::Awaitable Executor::Schedule() {
  return {this};
}

class Executor::TimeAwaiter {
 public:
  TimeAwaiter(Executor *executor, Executor::Duration duration)
      : executor_(executor), duration_(duration) {}

  constexpr bool await_ready() const noexcept { return false; }

  template<typename PromiseType>
  void await_suspend(std::coroutine_handle<PromiseType> continuation) {
    auto func = [continuation]() mutable {
      continuation.resume();
    };
    executor_->Schedule(func, duration_);
  }

  void await_resume() const noexcept {}

 private:
  Executor *executor_;
  Executor::Duration duration_;
};

class Executor::TimeAwaitable {
 public:
  TimeAwaitable(Executor *executor, Executor::Duration duration)
      : executor_(executor), duration_(duration) {}

  auto CoAwait(Executor *) { return TimeAwaiter(executor_, duration_); }
 private:
  Executor *executor_;
  Executor::Duration duration_;
};

Executor::TimeAwaitable Executor::ScheduleAfter(Executor::Duration duration) {
  return {this, duration};
}

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_EXECUTOR_EXECUTOR_HPP_
