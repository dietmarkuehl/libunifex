/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/async_trace.hpp>
#include <unifex/coroutine_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/config.hpp>

#if UNIFEX_NO_COROUTINES
# error "C++20 coroutine support is required to use <unifex/awaitable_sender.hpp>"
#endif

#include <experimental/coroutine>
#include <optional>
#include <type_traits>

namespace unifex {

namespace detail {

struct sender_task {
  struct promise_type {
    template <typename Awaitable, typename Receiver>
    explicit promise_type(Awaitable&, Receiver& r) noexcept
        : info_(continuation_info::from_continuation(r)) {}

    sender_task get_return_object() noexcept {
      return sender_task{
          std::experimental::coroutine_handle<promise_type>::from_promise(
              *this)};
    }
    std::experimental::suspend_always initial_suspend() noexcept {
      return {};
    }
    [[noreturn]] std::experimental::suspend_always final_suspend() noexcept {
      std::terminate();
    }
    [[noreturn]] void unhandled_exception() noexcept {
      std::terminate();
    }
    [[noreturn]] void return_void() noexcept {
      std::terminate();
    }
    template <typename Func>
    auto yield_value(Func&& func) noexcept {
      struct awaiter {
        Func&& func_;
        bool await_ready() noexcept {
          return false;
        }
        void await_suspend(std::experimental::coroutine_handle<>) {
          std::invoke((Func &&) func_);
        }
        [[noreturn]] void await_resume() noexcept {
          std::terminate();
        }
      };
      return awaiter{(Func &&) func};
    }

    template <typename Func>
    friend void
    tag_invoke(tag_t<visit_continuations>, const promise_type& p, Func&& func) {
      std::invoke(func, p.info_);
    }

    continuation_info info_;
  };

  std::experimental::coroutine_handle<promise_type> coro_;

  explicit sender_task(
      std::experimental::coroutine_handle<promise_type> coro) noexcept
      : coro_(coro) {}

  sender_task(sender_task&& other) noexcept
      : coro_(std::exchange(other.coro_, {})) {}

  ~sender_task() {
    if (coro_)
      coro_.destroy();
  }

  void start() noexcept {
    coro_.resume();
  }
};

} // namespace detail

template <typename Awaitable>
struct awaitable_sender {
  Awaitable awaitable_;

  using result_type = await_result_t<Awaitable>;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = std::conditional_t<
      std::is_void_v<result_type>,
      Variant<Tuple<>>,
      Variant<Tuple<std::remove_cvref_t<result_type>>>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  template <typename Receiver>
  detail::sender_task connect(Receiver&& receiver) && {
    return std::invoke(
        [](Awaitable awaitable,
           std::remove_cvref_t<Receiver> receiver) -> detail::sender_task {
          std::exception_ptr ex;
          try {
            if constexpr (std::is_void_v<result_type>) {
              co_await(Awaitable &&) awaitable;
              co_yield[&] {
                cpo::set_value(std::move(receiver));
              };
            } else {
              // This is a bit mind bending control-flow wise.
              // We are first evaluating the co_await expression.
              // Then the result of that is passed into std::invoke
              // which curries a reference to the result into another
              // lambda which is then returned to 'co_yield'.
              // The 'co_yield' expression then invokes this lambda
              // after the coroutine is suspended so that it is safe
              // for the receiver to destroy the coroutine.
              co_yield std::invoke(
                  [&](result_type&& result) {
                    return [&] {
                      cpo::set_value(
                          std::move(receiver), (result_type &&) result);
                    };
                  },
                  co_await(Awaitable &&) awaitable);
            }
          } catch (...) {
            ex = std::current_exception();
          }
          co_yield[&] {
            cpo::set_error(std::move(receiver), std::move(ex));
          };
        },
        (Awaitable &&) awaitable_,
        (Receiver &&) receiver);
  }
};

template <typename Awaitable>
awaitable_sender(Awaitable &&)
    ->awaitable_sender<std::remove_cvref_t<Awaitable>>;

} // namespace unifex
