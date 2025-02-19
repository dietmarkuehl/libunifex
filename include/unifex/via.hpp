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

#include <unifex/config.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/submit.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/get_stop_token.hpp>

#include <tuple>
#include <utility>
#include <exception>
#include <type_traits>

namespace unifex {

template <typename Predecessor, typename Successor>
struct via_sender {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Successor succ_;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types =
      typename Predecessor::template value_types<Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = concat_unique_t<
      typename Predecessor::template error_types<Variant>,
      typename Successor::template error_types<Variant>>;

  friend constexpr blocking_kind tag_invoke(
      tag_t<cpo::blocking>,
      const via_sender& sender) {
    const auto predBlocking = cpo::blocking(sender.pred_);
    const auto succBlocking = cpo::blocking(sender.succ_);
    if (predBlocking == blocking_kind::never &&
        succBlocking == blocking_kind::never) {
      return blocking_kind::never;
    } else if (
        predBlocking == blocking_kind::always_inline &&
        predBlocking == blocking_kind::always_inline) {
      return blocking_kind::always_inline;
    } else if (
        (predBlocking == blocking_kind::always_inline ||
         predBlocking == blocking_kind::always) &&
        (succBlocking == blocking_kind::always_inline ||
         succBlocking == blocking_kind::always)) {
      return blocking_kind::always;
    } else {
      return blocking_kind::maybe;
    }
  }

  template <typename Receiver, typename... Values>
  struct value_receiver {
    UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    void value() noexcept {
      std::apply(
          [&](Values && ... values) noexcept {
            cpo::set_value(
                std::forward<Receiver>(receiver_), (Values &&) values...);
          },
          std::move(values_));
    }

    template <typename Error>
    void error(Error&& error) noexcept {
      cpo::set_error(std::forward<Receiver>(receiver_), (Error &&) error);
    }

    void done() noexcept {
      cpo::set_done(std::forward<Receiver>(receiver_));
    }

    template <
        typename CPO,
        std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
    friend auto tag_invoke(CPO cpo, const value_receiver& r) noexcept(
        std::is_nothrow_invocable_v<CPO, const Receiver&>)
        -> std::invoke_result_t<CPO, const Receiver&> {
      return std::move(cpo)(std::as_const(r.receiver_));
    }

    template <typename Func>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const value_receiver& r,
        Func&& func) {
      std::invoke(func, r.receiver_);
    }
  };

  template <typename Receiver, typename Error>
  struct error_receiver {
    UNIFEX_NO_UNIQUE_ADDRESS Error error_;
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    void value() noexcept {
      cpo::set_error(std::forward<Receiver>(receiver_), std::move(error_));
    }

    template <typename OtherError>
    void error(OtherError&& otherError) noexcept {
      cpo::set_error(
          std::forward<Receiver>(receiver_), (OtherError &&) otherError);
    }

    void done() noexcept {
      cpo::set_done(std::forward<Receiver>(receiver_));
    }

    template <
        typename CPO,
        std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
    friend auto tag_invoke(CPO cpo, const error_receiver& r) noexcept(
        std::is_nothrow_invocable_v<CPO, const Receiver&>)
        -> std::invoke_result_t<CPO, const Receiver&> {
      return std::move(cpo)(std::as_const(r.receiver_));
    }

    template <typename Func>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const error_receiver& r,
        Func&& func) {
      std::invoke(func, r.receiver_);
    }
  };

  template <typename Receiver>
  struct done_receiver {
    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

    void value() noexcept {
      cpo::set_done(std::forward<Receiver>(receiver_));
    }

    template <typename OtherError>
    void error(OtherError&& otherError) noexcept {
      cpo::set_error(
          std::forward<Receiver>(receiver_), (OtherError &&) otherError);
    }

    void done() noexcept {
      cpo::set_done(std::forward<Receiver>(receiver_));
    }

    template <
        typename CPO,
        std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
    friend auto tag_invoke(CPO cpo, const done_receiver& r) noexcept(
        std::is_nothrow_invocable_v<CPO, const Receiver&>)
        -> std::invoke_result_t<CPO, const Receiver&> {
      return std::move(cpo)(std::as_const(r.receiver_));
    }

    template <typename Func>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const done_receiver& r,
        Func&& func) {
      std::invoke(func, r.receiver_);
    }
  };

  template <typename Receiver>
  struct predecessor_receiver {
    Successor successor_;
    Receiver receiver_;

    template <typename... Values>
    void value(Values&&... values) && noexcept {
      try {
        submit(
            (Successor &&) successor_,
            value_receiver<Receiver, std::remove_cvref_t<Values>...>{
                {(Values &&) values...}, (Receiver &&) receiver_});
      } catch (...) {
        cpo::set_error(
            static_cast<Receiver&&>(receiver_), std::current_exception());
      }
    }

    template <typename Error>
    void error(Error&& error) && noexcept {
      try {
        submit(
            (Successor &&) successor_,
            error_receiver<Receiver, std::remove_cvref_t<Error>>{
                (Error &&) error, (Receiver &&) receiver_});
      } catch (...) {
        cpo::set_error(
            static_cast<Receiver&&>(receiver_), std::current_exception());
      }
    }

    void done() && noexcept {
      try {
        submit(
            (Successor &&) successor_,
            done_receiver<Receiver>{(Receiver &&) receiver_});
      } catch (...) {
        cpo::set_error(
            static_cast<Receiver&&>(receiver_), std::current_exception());
      }
    }

    template <
        typename CPO,
        std::enable_if_t<!cpo::is_receiver_cpo_v<CPO>, int> = 0>
    friend auto tag_invoke(CPO cpo, const predecessor_receiver& r) noexcept(
        std::is_nothrow_invocable_v<CPO, const Receiver&>)
        -> std::invoke_result_t<CPO, const Receiver&> {
      return std::move(cpo)(std::as_const(r.receiver_));
    }

    template <typename Func>
    friend void tag_invoke(
        tag_t<visit_continuations>,
        const predecessor_receiver& r,
        Func&& func) {
      std::invoke(func, r.receiver_);
    }
  };

  template <typename Receiver>
  auto connect(Receiver&& receiver) && {
    return cpo::connect(
        static_cast<Predecessor&&>(pred_),
        predecessor_receiver<
            std::remove_cvref_t<Receiver>>{static_cast<Successor&&>(succ_),
                                           static_cast<Receiver&&>(receiver)});
  }
};

template <typename Predecessor, typename Successor>
auto via(Successor&& succ, Predecessor&& pred) {
  return via_sender<
      std::remove_cvref_t<Predecessor>,
      std::remove_cvref_t<Successor>>{(Predecessor &&) pred,
                                      (Successor &&) succ};
}

} // namespace unifex
