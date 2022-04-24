cmake_minimum_required(VERSION 3.16)
project(mini_async_simple)

set(CMAKE_CXX_STANDARD 20)

set(AS_INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(AS_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(AS_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)
set(GTEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest)

add_subdirectory(${GTEST_DIR})

add_library(async_simple)
target_sources(async_simple
        PUBLIC
            ${AS_INC_DIR}/async_simple/base/assert.hpp
            ${AS_INC_DIR}/async_simple/base/macro.hpp
            ${AS_INC_DIR}/async_simple/base/noncopyable.hpp
            ${AS_INC_DIR}/async_simple/base/unit.hpp
            ${AS_INC_DIR}/async_simple/base/try.hpp
            ${AS_INC_DIR}/async_simple/base/try_variant.hpp
            ${AS_INC_DIR}/async_simple/base/move_wrapper.hpp
            ${AS_INC_DIR}/async_simple/container/threadsafe_queue.hpp
            ${AS_INC_DIR}/async_simple/util/thread_pool.hpp
            ${AS_INC_DIR}/async_simple/executor/io_executor.hpp
            ${AS_INC_DIR}/async_simple/executor/executor.hpp
            ${AS_INC_DIR}/async_simple/executor/simple_io_executor.hpp
            ${AS_INC_DIR}/async_simple/executor/simple_executor.hpp
            ${AS_INC_DIR}/async_simple/sync/future_trait.hpp
            ${AS_INC_DIR}/async_simple/sync/future_state.hpp
            ${AS_INC_DIR}/async_simple/sync/local_state.hpp
            ${AS_INC_DIR}/async_simple/sync/future.hpp
            ${AS_INC_DIR}/async_simple/sync/promise.hpp
            ${AS_INC_DIR}/async_simple/sync/future_helper.hpp
            ${AS_INC_DIR}/async_simple/coro/coro_concept.hpp
            ${AS_INC_DIR}/async_simple/coro/detached_coroutine.hpp
            ${AS_INC_DIR}/async_simple/coro/ready_awaiter.hpp
            ${AS_INC_DIR}/async_simple/coro/future_awaiter.hpp
            ${AS_INC_DIR}/async_simple/coro/count_event.hpp
            ${AS_INC_DIR}/async_simple/coro/lazy.hpp
            ${AS_INC_DIR}/async_simple/coro/sleep.hpp
            ${AS_INC_DIR}/async_simple/coro/via_coroutine.hpp
            ${AS_INC_DIR}/async_simple/coro/collect.hpp
        PRIVATE
            ${AS_SRC_DIR}/as.cpp
        )
target_include_directories(async_simple
        PUBLIC
            ${AS_INC_DIR})

add_executable(async_simple_test
        ${AS_TEST_DIR}/async_simple_test.hpp
        ${AS_TEST_DIR}/common.hpp
        ${AS_TEST_DIR}/async_simple_test.cpp
        ${AS_TEST_DIR}/base/try_test.cpp
        ${AS_TEST_DIR}/base/try_variant_test.cpp
        ${AS_TEST_DIR}/sync/future_state_test.cpp
        ${AS_TEST_DIR}/sync/future_test.cpp
        )
target_include_directories(async_simple_test
        PRIVATE
            ${AS_TEST_DIR})
target_link_libraries(async_simple_test
        PRIVATE
            async_simple
            gtest
            gmock)
