#ifndef MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_COLLECT_HPP_
#define MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_COLLECT_HPP_

#include "async_simple/coro/count_event.hpp"
#include "async_simple/coro/lazy.hpp"

#include <memory>

namespace async_simple::coro {

namespace detail {

template<typename T>
struct CollectAnyResult : noncopyable {
  CollectAnyResult() : index(static_cast<std::size_t>(-1)), value() {}
  CollectAnyResult(std::size_t _index, T &&_value)
      : index(_index), value(std::move(_value)) {}

  CollectAnyResult(CollectAnyResult &&other)
      : index(std::exchange(other.index, static_cast<std::size_t>(-1))),
        value(std::move(other.value)) {}

  std::size_t index;
  Try<T> value;
};

template<>
struct CollectAnyResult<void> {
  CollectAnyResult() : index(static_cast<std::size_t>(-1)), value() {}

  std::size_t index;
  Try<void> value;
};

template<typename LazyType, typename InAlloc>
struct CollectAnyAwaiter : noncopyable {
  using ValueType = typename LazyType::ValueType;
  using ResultType = CollectAnyResult<ValueType>;

  CollectAnyAwaiter(std::vector<LazyType, InAlloc> &&input)
      : input(std::move(input)), result(nullptr) {}

  CollectAnyAwaiter(CollectAnyAwaiter &&other)
      : input(std::move(other.input)), result(std::move(other.result)) {}

  bool await_ready() const noexcept {
    return input.empty() || (result && result->index != static_cast<std::size_t>(-1));
  }

  void await_suspend(std::coroutine_handle<> continuation) {
    auto promise = std::coroutine_handle<LazyPromiseBase>::from_address(
        continuation.address()).promise();
    auto executor = promise.executor_;
    std::vector<LazyType, InAlloc> in(std::move(input));
    auto res = std::make_shared<ResultType>();
    auto event = std::make_shared<CountEvent>(in.size());
    result = res;
    for (size_t i = 0; i < in.size() && (res->index == static_cast<std::size_t>(-1)); ++i) {
      if (!in[i].coro_.promise().executor_) {
        in[i].coro_.promise().executor_ = executor;
      }
      in[i].Start([i, sz = in.size(), res, continuation, event](Try<ValueType> &&t) mutable {
        ASSERT(event != nullptr);
        auto count = event->DownCount();
        if (count == sz + 1) {
          res->index = i;
          res->value = std::move(t);
          continuation.resume();
        }
      });
    }
  }

  auto await_resume() {
    ASSERT(result != nullptr);
    return std::move(*result);
  }

  std::vector<LazyType, InAlloc> input;
  std::shared_ptr<ResultType> result;
};

template<typename T, typename InAlloc>
struct SimpleCollectAnyAwaitable {
  using ValueType = T;
  using LazyType = Lazy<T>;
  using VectorType = std::vector<LazyType, InAlloc>;

  SimpleCollectAnyAwaitable(VectorType &&_input)
      : input(std::move(_input)) {}

  auto CoAwait(Executor *executor) {
    return CollectAnyAwaiter<LazyType, InAlloc>(std::move(input));
  }

  VectorType input;
};

template<typename LazyType, typename IAlloc, typename OAlloc, bool Para = false>
struct CollectAllAwaiter : noncopyable {
  using ValueType = typename LazyType::ValueType;

  CollectAllAwaiter(std::vector<LazyType, IAlloc> &&in, OAlloc out_alloc)
      : input(std::move(in)), output(out_alloc), event(input.size()) {
    output.resize(input.size());
  }

  CollectAllAwaiter(CollectAllAwaiter &&other)
      : input(std::move(other.input)),
        output(std::move(other.output)),
        event(std::move(other.event)) {}

  bool await_ready() const noexcept { return input.empty(); }
  void await_suspend(std::coroutine_handle<> continuation) {
    auto promise = std::coroutine_handle<LazyPromiseBase>::from_address(
        continuation.address()).promise();
    auto executor = promise.executor_;
    for (size_t i = 0; i < input.size(); ++i) {
      auto &exec = input[i].coro_.promise().executor_;
      if (!exec) {
        exec = executor;
      }
      auto &&func = [this, i]() {
        input[i].Start([this, i](Try<ValueType> &&t) {
          output[i] = std::move(t);
          auto awaiting_coro = event.Down();
          if (awaiting_coro) {
            awaiting_coro.resume();
          }
        });
      };
      if (Para && input.size() > 1) {
        if (executor != nullptr) LIKELY {
          executor->Schedule(func);
          continue;
        }
      }
      func();
    }
    event.SetAwaitingCoroutine(continuation);
    auto awaiting_coro = event.Down();
    if (awaiting_coro) {
      awaiting_coro.resume();
    }
  }
  auto await_resume() { return std::move(output); }

  std::vector<LazyType, IAlloc> input;
  std::vector<Try<ValueType>, OAlloc> output;
  CountEvent event;
};

template<typename T, typename IAlloc, typename OAlloc, bool Para = false>
struct SimpleCollectAllAwaitable {
  using ValueType = T;
  using LazyType = Lazy<T>;
  using VectorType = std::vector<LazyType, IAlloc>;

  SimpleCollectAllAwaitable(VectorType &&_input, OAlloc _out_alloc)
      : input(std::move(_input)), out_alloc(_out_alloc) {}

  auto CoAwait(Executor *executor) {
    return CollectAllAwaiter<LazyType, IAlloc, OAlloc, Para>(std::move(input), out_alloc);
  }

  VectorType input;
  OAlloc out_alloc;
};

} // namespace async::simple::detail

template<typename T, template<typename> typename LazyType,
    typename IAlloc = std::allocator<LazyType<T>>>
inline auto CollectAny(std::vector<LazyType<T>, IAlloc> &&input)
-> Lazy<detail::CollectAnyResult<T>> {
  using AT = std::conditional_t<std::is_same_v<LazyType<T>, Lazy<T>>,
                                detail::SimpleCollectAnyAwaitable<T, IAlloc>,
                                detail::CollectAnyAwaiter<LazyType<T>, IAlloc>>;
  co_return co_await AT(std::move(input));
}

namespace detail {

template<bool Para, typename T, template<typename> typename LazyType,
    typename IAlloc = std::allocator<LazyType<T>>,
    typename OAlloc = std::allocator<Try<T>>>
inline auto CollectAllImpl(std::vector<LazyType<T>, IAlloc> &&input,
                           OAlloc out_alloc = OAlloc())
-> Lazy<std::vector<Try<T>, OAlloc>> {
  using AT = std::conditional_t<std::is_same_v<LazyType<T>, Lazy<T>>,
                                detail::SimpleCollectAllAwaitable<T, IAlloc, OAlloc, Para>,
                                detail::CollectAllAwaiter<LazyType<T>, IAlloc, OAlloc, Para>>;
  co_return co_await AT(std::move(input), out_alloc);
}

template<bool Para, typename T, template<typename> typename LazyType,
    typename IAlloc = std::allocator<LazyType<T>>,
    typename OAlloc = std::allocator<Try<T>>>
inline auto CollectAllWindowedImpl(size_t max_concurrency,
                                   bool yield,
                                   std::vector<LazyType<T>, IAlloc> &&input,
                                   OAlloc out_alloc = OAlloc())
-> Lazy<std::vector<Try<T>, OAlloc>> {
  using AT = std::conditional_t<std::is_same_v<LazyType<T>, Lazy<T>>,
                                detail::SimpleCollectAllAwaitable<T, IAlloc, OAlloc, Para>,
                                detail::CollectAllAwaiter<LazyType<T>, IAlloc, OAlloc, Para>>;
  std::vector<Try<T>, OAlloc> output(out_alloc);
  std::size_t input_size = input.size();
  if (max_concurrency == 0 || input_size <= max_concurrency) {
    co_return co_await AT(std::move(input), out_alloc);
  }
  std::size_t start = 0;
  while (start < input_size) {
    std::size_t end = std::min(input_size, start + max_concurrency);
    std::vector<LazyType<T>, IAlloc> tmp_group(input.get_allocator());
    for (; start < end; ++start) {
      tmp_group.push_back(std::move(input[start]));
    }
    auto tmp_output = co_await AT(std::move(tmp_group), out_alloc);
    for (auto &t : tmp_output) {
      output.push_back(std::move(t));
    }
    if (yield) {
      co_await Yield{};
    }
  }
  co_return std::move(output);
}

template<template<typename> typename LazyType, typename Ts>
Lazy<void> MakeWrapperTask(LazyType<Ts> &&awaitable, Try<Ts> &result) {
  try {
    if constexpr(std::is_void_v<Ts>) {
      co_await awaitable;
    } else {
      result = co_await awaitable;
    }
  } catch (...) {
    result.SetException(std::current_exception());
  }
}

template<bool Para,
    template<typename> typename LazyType,
    typename ...Ts,
    size_t ...Indices>
inline auto CollectAllVariadicImpl(std::index_sequence<Indices...>,
                                   LazyType<Ts> &&...awaitables)
-> Lazy<std::tuple<Try<Ts>...>> {
  static_assert(sizeof...(Ts) > 0);

  std::tuple<Try<Ts>...> results;
  std::vector<Lazy<void>> wrapper_tasks;
  (..., wrapper_tasks.push_back(std::move(
      MakeWrapperTask(std::move(awaitables), std::get<Indices>(results)))));
  co_await CollectAllImpl<Para>(std::move(wrapper_tasks));
  co_return std::move(results);
}

} // namespace async_simple::coro::detail

template<typename T,
    template<typename> typename LazyType,
    typename IAlloc = std::allocator<LazyType<T>>,
    typename OAlloc = std::allocator<Try<T>>>
inline auto CollectAll(std::vector<LazyType<T>, IAlloc> &&input,
                       OAlloc out_alloc = OAlloc{})
-> Lazy<std::vector<Try<T>, OAlloc>> {
  co_return co_await detail::CollectAllImpl<false>(std::move(input), out_alloc);
}

template<typename T,
    template<typename> typename LazyType,
    typename IAlloc = std::allocator<LazyType<T>>,
    typename OAlloc = std::allocator<Try<T>>>
inline auto CollectAllPara(std::vector<LazyType<T>, IAlloc> &&input,
                           OAlloc out_alloc = OAlloc{})
-> Lazy<std::vector<Try<T>, OAlloc>> {
  co_return co_await detail::CollectAllImpl<true>(std::move(input), out_alloc);
}

template<template<typename> typename LazyType, typename ...Ts>
inline auto CollectAll(LazyType<Ts> &&...inputs)
-> Lazy<std::tuple<Try<Ts>...>> {
  if constexpr(sizeof...(Ts) == 0) {
    co_return std::tuple<>{};
  } else {
    co_return co_await detail::CollectAllVariadicImpl<false>(
        std::make_index_sequence<sizeof...(Ts)>{}, std::move(inputs)...);
  }
}

template<template<typename> typename LazyType, typename ...Ts>
inline auto CollectAllPara(LazyType<Ts> &&...inputs)
-> Lazy<std::tuple<Try<Ts>...>> {
  if constexpr(sizeof...(Ts) == 0) {
    co_return std::tuple<>{};
  } else {
    co_return co_await detail::CollectAllVariadicImpl<true>(
        std::make_index_sequence<sizeof...(Ts)>{}, std::move(inputs)...);
  }
}

template<typename T,
    template<typename> typename LazyType,
    typename IAlloc = std::allocator<LazyType<T>>,
    typename OAlloc = std::allocator<Try<T>>>
inline auto CollectAllWindowed(std::size_t max_concurrency,
                               bool yield,
                               std::vector<LazyType<T>, IAlloc> &&input,
                               OAlloc out_alloc = OAlloc{})
-> Lazy<std::vector<Try<T>, OAlloc>> {
  co_return co_await detail::CollectAllWindowedImpl<false>(
      max_concurrency, yield, std::move(input), out_alloc);
}

template<typename T,
    template<typename> typename LazyType,
    typename IAlloc = std::allocator<LazyType<T>>,
    typename OAlloc = std::allocator<Try<T>>>
inline auto CollectAllWindowedPara(std::size_t max_concurrency,
                                   bool yield,
                                   std::vector<LazyType<T>, IAlloc> &&input,
                                   OAlloc out_alloc = OAlloc{})
-> Lazy<std::vector<Try<T>, OAlloc>> {
  co_return co_await detail::CollectAllWindowedImpl<true>(
      max_concurrency, yield, std::move(input), out_alloc);
}

} // namespace async_simple::coro

#endif // MINI_ASYNC_SIMPLE_INCLUDE_ASYNC_SIMPLE_CORO_COLLECT_HPP_
