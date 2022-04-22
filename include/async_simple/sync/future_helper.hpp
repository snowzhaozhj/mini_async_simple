#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_FUTURE_HELPER_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_FUTURE_HELPER_HPP_

#include "async_simple/sync/future.hpp"

#include <memory>

namespace async_simple {

template<typename Iter>
inline
Future<std::vector<Try<typename std::iterator_traits<Iter>::value_type::value_type>>>
CollectAll(Iter begin, Iter end) {
  using T = typename std::iterator_traits<Iter>::value_type::value_type;
  size_t n = std::distance(begin, end);
  bool all_ready = std::all_of(begin, end, [](const typename std::iterator_traits<Iter>::value_type &t) {
    return t.HasResult();
  });
  if (all_ready) {
    std::vector<Try<T>> results;
    results.reserve(n);
    for (auto it = begin; it != end; ++it) {
      results.push_back(std::move(it->Result()));
    }
    return Future<std::vector<Try<T>>>(std::move(results));
  }

  Promise<std::vector<Try<T>>> promise;
  auto future = promise.GetFuture();

  struct Context {
    Context(std::size_t n, Promise<std::vector<Try<T>>> pp)
        : results(n), p(std::move(pp)) {}
    ~Context() { p.SetValue(std::move(results)); }
    std::vector<Try<T>> results;
    Promise<std::vector<Try<T>>> p;
  };

  auto ctx = std::make_shared<Context>(n, std::move(promise));
  for (std::size_t i = 0; i < n; ++i) {
    auto cur = begin + i;
    if (cur->HasResult()) {
      ctx->results[i] = std::move(cur->Result());
    } else {
      cur->SetContinuation([ctx, i](Try<T> &&t) mutable {
        ctx->results[i] = std::move(t);
      });
    }
  }
  return future;
}

template<typename T>
Future<T> MakeReadyFuture(T &&v) {
  return Future<T>(Try<T>(std::forward<T>(v)));
}
template<typename T>
Future<T> MakeReadyFuture(Try<T> &&t) {
  return Future<T>(std::move(t));
}
template<typename T>
Future<T> MakeReadyFuture(std::exception_ptr ex) {
  return Future<T>(Try<T>(ex));
}

} // namespace async_simple

#endif // MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_SYNC_FUTURE_HELPER_HPP_
