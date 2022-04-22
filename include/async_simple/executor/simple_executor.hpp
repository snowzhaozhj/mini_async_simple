#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_EXECUTOR_SIMPLE_EXECUTOR_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_EXECUTOR_SIMPLE_EXECUTOR_HPP_

#include "async_simple/executor/executor.hpp"
#include "async_simple/executor/simple_io_executor.hpp"
#include "async_simple/util/thread_pool.hpp"

namespace async_simple::executors {

class SimpleExecutor : public Executor {
  static constexpr int64_t kContextMask = 0x40000000;

 public:
  explicit SimpleExecutor(std::size_t thread_num) {
    io_executor_.Init();
  }
  ~SimpleExecutor() {
    io_executor_.Destory();
  }

  bool Schedule(Func func) override {
    return pool_.ScheduleById(std::move(func)) == util::ThreadPool::kErrorNone;
  }
  [[nodiscard]] bool CurrentThreadInExecutor() const override {
    return pool_.GetCurrentId() != -1;
  }
  [[nodiscard]] ExecutorStat Stat() const override {
    return {};
  }
  [[nodiscard]] size_t CurrentContextId() const override {
    return pool_.GetCurrentId();
  }

  Context Checkout() override {
    return reinterpret_cast<Context>(pool_.GetCurrentId() | kContextMask);
  }
  bool Checkin(Func func, Context context, ScheduleOptions options) override {
    auto id = reinterpret_cast<int64_t>(context);
    auto prompt = pool_.GetCurrentId() == (id & (~kContextMask)) && options.prompt;
    if (prompt) {
      func();
      return true;
    }
    return pool_.ScheduleById(std::move(func), id & (~kContextMask)) == util::ThreadPool::kErrorNone;
  }

  IOExecutor *GetIOExecutor() override { return &io_executor_; }

 private:
  util::ThreadPool pool_;
  SimpleIOExecutor io_executor_;
};

} // namespace async_simple

#endif // MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_EXECUTOR_SIMPLE_EXECUTOR_HPP_
