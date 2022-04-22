#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_EXECUTOR_SIMPLE_IO_EXECUTOR_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_EXECUTOR_SIMPLE_IO_EXECUTOR_HPP_

#include "async_simple/executor/io_executor.hpp"

#include <thread>
#include <utility>

#ifdef HAS_AIO
#include <libaio.h>
#endif

namespace async_simple::executors {

#ifdef HAS_AIO
class SimpleIOExecutor : public IOExecutor {
 public:
  static constexpr int kMaxAio = 8;

  SimpleIOExecutor() = default;
  ~SimpleIOExecutor() override = default;

  class Task {
   public:
    Task(AIOCallback func) : aio_callback_(std::move(func)) {}
    ~Task() = default;

    void Process(io_event_t &event) { aio_callback_(event); }

   private:
    AIOCallback aio_callback_;
  };

  bool Init() {
    auto r = io_setup(kMaxAio, &io_context_);
    if (r < 0) return false;
    loop_thread_ = std::thread([this]() {
      this->Loop();
    });
    return true;
  }

  void Destroy() {
    shutdown_ = true;
    if (loop_thread_.joinable()) {
      loop_thread_.join();
    }
    io_destroy(io_context_);
  }

  void Loop() {
    while (shutdown_) {
      io_event events[kMaxAio];
      struct timespec timeout = {0, 1000 * 300};
      auto n = io_getevents(io_context_, 1, kMaxAio, events, &timeout);
      if (n < 0) continue;
      for (int i = 0; i < n; ++i) {
        auto task = reinterpret_cast<Task *>(events[i].data);
        io_event_t event{events[i].data, events[i].obj, events[i].res, events[i].res2};
        task->Process(event);
        delete task;
      }
    }
  }

  void SubmitIO(int fd, iocb_cmd cmd, void *buffer, size_t len, off_t offset, AIOCallback cb) override {
    iocb io;
    memset(&io, 0, sizeof(iocb));
    io.aio_fildes = fd;
    io.aio_lio_opcode = cmd;
    io.u.c.buf = buffer;
    io.u.c.nbytes = len;
    io.data = new Task(cb);
    struct iocb *iocbs[] = {&io};
    auto r = io_submit(io_context_, 1, iocbs);
    if (r < 0) {
      auto task = reinterpret_cast<Task *>(iocbs[0]->data);
      io_event_t event;
      event.res = r;
      task->Process(event);
      delete task;
    }
  }

  void SubmitIOV(int fd, iocb_cmd cmd, const iovec_t *iov, size_t count, off_t offset, AIOCallback cb) override {
    iocb io;
    memset(&io, 0, sizeof(iocb));
    io.aio_fildes = fd;
    io.aio_lio_opcode = cmd;
    io.u.c.buf = (void*)iov;
    io.u.c.offset = offset;
    io.u.c.nbytes = count;
    io.data = new Task(cb);
    struct iocb *iocbs[] = {&io};
    auto r = io_submit(io_context_, 1, iocbs);
    if (r < 0) {
      auto task = reinterpret_cast<Task *>(iocbs[0]->data);
      io_event_t event;
      event.res = r;
      task->Process(event);
      delete task;
    }
  }
 private:
  volatile bool shutdown_{false};
  io_context_t io_context_{nullptr};
  std::thread loop_thread_;
};
#else

class SimpleIOExecutor : public IOExecutor {
 public:
  SimpleIOExecutor() = default;
  ~SimpleIOExecutor() override = default;

  bool Init() { return false; }
  void Destory() {}
  void Loop() {}
  void SubmitIO(int fd, iocb_cmd cmd, void *buffer, size_t len, off_t offset, AIOCallback cb) override {}
  void SubmitIOV(int fd, iocb_cmd cmd, const iovec_t *iov, size_t count, off_t offset, AIOCallback cb) override {}
};

#endif

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_EXECUTOR_SIMPLE_IO_EXECUTOR_HPP_
