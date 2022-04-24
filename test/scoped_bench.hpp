#ifndef MINI_ASYNC_SIMPLE_TEST_SCOPED_BENCH_HPP_
#define MINI_ASYNC_SIMPLE_TEST_SCOPED_BENCH_HPP_

#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <utility>

class ScopedBench {
 public:
  explicit ScopedBench(std::string msg, int loop)
      : msg_(std::move(msg)), loop_(loop) {
    start_time_ = std::chrono::steady_clock::now().time_since_epoch().count();
  }
  ~ScopedBench() {
    auto d = std::chrono::steady_clock::now().time_since_epoch();
    auto mic = duration_cast<std::chrono::nanoseconds>(d);
    long time_ns = ((mic.count() - start_time_) / loop_);
    double time_us = time_ns / 1000.0;
    double time_ms = time_us / 1000.0;
    if (time_ms > 100) {
      std::cout << std::right << std::setw(30) << msg_.data() << ": "
                << time_ms << " ms" << std::endl;
    } else if (time_us > 100) {
      std::cout << std::right << std::setw(30) << msg_.data() << ": "
                << time_us << " us" << std::endl;
    } else {
      std::cout << std::right << std::setw(30) << msg_.data() << ": "
                << time_ns << " ns" << std::endl;
    }
  }

  int64_t start_time_;
  std::string msg_;
  int loop_;
};

#endif // MINI_ASYNC_SIMPLE_TEST_SCOPED_BENCH_HPP_
