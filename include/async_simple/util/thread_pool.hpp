#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_UTIL_THREAD_POOL_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_UTIL_THREAD_POOL_HPP_

#include <functional>

#include "async_simple/base/assert.hpp"
#include "async_simple/container/threadsafe_queue.hpp"

namespace async_simple::util {

class ThreadPool {
 public:
  struct WorkItem {
    bool can_steal{false};
    std::function<void()> fn{nullptr};
  };

  enum ErrorType {
    kErrorNone,
    kErrorPoolHasStop,
    kErrorPoolItemIsNull,
  };

  explicit ThreadPool(std::size_t thread_num = std::thread::hardware_concurrency(),
                      bool enable_work_steal = false)
      : thread_num_(thread_num ? thread_num : std::thread::hardware_concurrency()),
        queues_(thread_num_),
        enable_work_steal_(enable_work_steal),
        stop_(false) {
    threads_.reserve(thread_num);
    for (int i = 0; i < thread_num; ++i) {
      threads_.emplace_back(&ThreadPool::WorkerThreadMain, this, i);
    }
  }
  ~ThreadPool() {
    stop_ = true;
    for (auto &queue : queues_) {
      queue.Stop();
    }
    for (auto &thread : threads_) {
      thread.join();
    }
  }

  ThreadPool::ErrorType ScheduleById(std::function<void()> fn,
                                     int32_t id = -1) {
    if (fn == nullptr) {
      return kErrorPoolItemIsNull;
    }
    if (stop_) {
      return kErrorPoolHasStop;
    }
    if (id == -1) {
      if (enable_work_steal_) {
        WorkItem work_item{true, std::move(fn)};
        for (int n = 0; n < thread_num_ * 2; ++n) {
          if (queues_.at(n % thread_num_).TryPush(work_item)) {
            return kErrorNone;
          }
        }
      }
      id = rand() % thread_num_;
      queues_[id].Push(WorkItem{enable_work_steal_, std::move(fn)});
    } else {
      ASSERT(id < thread_num_);
      queues_[id].Push(WorkItem{false, std::move(fn)});
    }
    return kErrorNone;
  }

  [[nodiscard]] int32_t GetCurrentId() const {
    auto current = GetCurrent();
    if (this == current->second) {
      return current->first;
    }
    return -1;
  }
  [[nodiscard]] std::size_t GetItemCount() const {
    std::size_t ret = 0;
    for (int i = 0; i < thread_num_; ++i) {
      ret += queues_[i].Size();
    }
    return ret;
  }
  [[nodiscard]] std::size_t GetThreadNum() const { return thread_num_; }

 private:
  [[nodiscard]] std::pair<int32_t, ThreadPool *> *GetCurrent() const {
    static thread_local std::pair<int32_t, ThreadPool *> current(-1, nullptr);
    return &current;
  }

  void WorkerThreadMain(int32_t id) {
    auto current = GetCurrent();
    current->first = id;
    current->second = this;
    while (true) {
      WorkItem item;
      if (enable_work_steal_) {
        // 优先进行窃取
        for (int n = 0; n < thread_num_ * 2; ++n) {
          if (queues_[(id + n) % thread_num_].TryPopIf(item, [](WorkItem &item) { return item.can_steal; })) {
            break;
          }
        }
      }
      // 没有开启窃取，或者窃取失败，则调用Pop等待任务
      if (!item.fn && !queues_[id].Pop(item)) {
        break;
      }
      if (item.fn) {
        item.fn();
      }
    }
  }

  std::size_t thread_num_;
  std::vector<container::ThreadsafeQueue<WorkItem>> queues_;
  std::vector<std::thread> threads_;
  bool enable_work_steal_;
  std::atomic<bool> stop_;
};

}

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_UTIL_THREAD_POOL_HPP_
