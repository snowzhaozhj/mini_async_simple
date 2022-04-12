cmake_minimum_required(VERSION 3.16)
project(mini_async_simple)

set(CMAKE_CXX_STANDARD 20)

set(AS_INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(AS_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(async_simple)
target_sources(async_simple
        PUBLIC
            ${AS_INC_DIR}/async_simple/base/assert.hpp
            ${AS_INC_DIR}/async_simple/base/macro.hpp
            ${AS_INC_DIR}/async_simple/base/noncopyable.hpp
            ${AS_INC_DIR}/async_simple/base/unit.hpp
            ${AS_INC_DIR}/async_simple/base/try.hpp
            ${AS_INC_DIR}/async_simple/base/try_variant.hpp
        PRIVATE
            ${AS_SRC_DIR}/as.cpp
        )
target_include_directories(async_simple
        PUBLIC
            ${AS_INC_DIR})
