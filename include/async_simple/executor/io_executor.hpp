#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_EXECUTOR_IO_EXECUTOR_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_EXECUTOR_IO_EXECUTOR_HPP_

#include "async_simple/base/noncopyable.hpp"

#include <cstdint>
#include <cstdlib>
#include <functional>

namespace async_simple {

/// IOExecutor负责接收和处理IO请求
/// 调用者会以callback的方式被通知
/// IO类型和参数的定义类似于Linux AIO

enum iocb_cmd {
  IOCB_CMD_PREAD = 0,
  IOCB_CMD_PWRITE = 1,
  IOCB_CMD_FSYNC = 2,
  IOCB_CMD_FDSYNC = 3,
  /* Experimental
   * IOCB_CMD_PREADX = 4,
   * IOCB_CMD_POLL = 5,
   * */
  IOCB_CMD_NOOP = 6,
  IOCB_CMD_PREADV = 7,
  IOCB_CMD_PWRITEV = 8,
};

struct io_event_t {
  void *data;
  void *obj;
  uint64_t res;
  uint64_t res2;
};

struct iovec_t {
  void *iov_base;
  size_t iov_len;
};

using AIOCallback = std::function<void(io_event_t &)>;

/// IOExecutor会接收IO读写请求
/// 用户应该将实现后的IOExecutor和实现后的Executor放在一起使用
class IOExecutor : noncopyable {
 public:
  using Func = std::function<void()>;

  IOExecutor() = default;
  virtual ~IOExecutor() = default;

  virtual void SubmitIO(int fd, iocb_cmd cmd, void *buffer, size_t length,
                        off_t offset, AIOCallback cb) = 0;
  virtual void SubmitIOV(int fd, iocb_cmd cmd, const iovec_t *iov, size_t coutn,
                         off_t offset, AIOCallback cb) = 0;
};

} // namespace async_simple

#endif //MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_EXECUTOR_IO_EXECUTOR_HPP_
