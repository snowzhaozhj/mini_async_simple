#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CONTAINER_THREADSAFE_QUEUE_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CONTAINER_THREADSAFE_QUEUE_HPP_

#include <queue>
#include <mutex>
#include <condition_variable>

namespace async_simple::container {

template<typename T> requires std::is_move_assignable_v<T>
class ThreadsafeQueue {
 public:
  ThreadsafeQueue() = default;

  void Push(T &&item) {
    {
      std::lock_guard guard(mutex_);
      queue_.push(std::move(item));
    }
    cond_.notify_one();
  }
  bool TryPush(const T &item) {
    {
      std::unique_lock lock(mutex_, std::try_to_lock);
      if (!lock) return false;
      queue_.push(item);
    }
    cond_.notify_one();
    return true;
  }

  bool Pop(T &item) {
    std::unique_lock lock(mutex_);
    cond_.wait(lock, [this]() {
      return !queue_.empty() || stop_;
    });
    if (queue_.empty()) {
      return false;
    }
    item = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  bool TryPop(T &item) {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock || queue_.empty()) {
      return false;
    }
    item = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  bool TryPopIf(T &item, bool (*predict)(T &) = nullptr) {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock || queue_.empty()) {
      return false;
    }
    if (predict && !predict(queue_.front())) {
      return false;
    }
    item = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  [[nodiscard]] std::size_t Size() const {
    std::lock_guard lg(mutex_);
    return queue_.size();
  }

  bool Empty() const {
    std::lock_guard lg(mutex_);
    return queue_.empty();
  }

  void Stop() {
    {
      std::lock_guard lg(mutex_);
      stop_ = true;
    }
    cond_.notify_all();
  }

 private:
  std::queue<T> queue_;
  mutable std::mutex mutex_;
  std::condition_variable cond_;
  bool stop_;
};

} // namespace async_simple::container

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CONTAINER_THREADSAFE_QUEUE_HPP_
