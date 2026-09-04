#pragma once

#include "compiler_util.h"
#include "string.h"
#include <cstdio>
#include <tuple>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace litestl::util {
template <StrLiteral typeName, StrLiteral msg> struct Error {
  static constexpr StrLiteral message = msg;
  static constexpr StrLiteral type = typeName;
};

template <typename... types> struct ErrorUnion {
  std::tuple<types...> errors;
};

template <typename T>
concept IsError = requires(T t) {
  { T::message } -> std::same_as<const char *>;
  { T::type } -> std::same_as<const char *>;
};

namespace detail {
class ErrorTag {};
} // namespace detail

template <StrLiteral typeName, StrLiteral msg> struct SuccessOrError {
  using error = Error<typeName, msg>;
  bool success;

  SuccessOrError() : success(false)
  {
  }
  SuccessOrError(bool success) : success(success)
  {
  }
  operator bool() const
  {
    return success;
  }

  void printError() const
  {
    printf("%s: %s\n", typeName.value, msg.value);
  }
};

template <typename T, StrLiteral typeName, StrLiteral msg>
struct alignas(ContainerAlign<T>) ValueOrError {
  using error = Error<typeName, msg>;
  using value_type = T;

  using failed_type = detail::ErrorTag;
  static failed_type failed;

  char storage[sizeof(T)];
  bool success = true;

  ValueOrError(value_type &&value) : success(true)
  {
    new (static_cast<void *>(storage)) value_type(std::forward_like(value));
  }
  // failed has no data, is simply used for type resolution
  ValueOrError(const failed_type &) : success(false)
  {
  }
  ValueOrError(ValueOrError &&other) : success(other.success)
  {
    if (other.success) {
      new (static_cast<void *>(storage)) value_type(std::move(*other));
    }
  }
  ValueOrError(const ValueOrError &other) = delete;
  ValueOrError &operator=(const ValueOrError &other) = delete;

  DEFAULT_MOVE_ASSIGNMENT(ValueOrError)

  T *operator*()
  {
    return reinterpret_cast<T *>(storage);
  }
};

/**
 * ValueOrErrors: a type that represents either a successful value of type T
 * or one of several possible error states identified by string literals.
 * OwnerMessage is a string literal describing the context or owner of the value.
 * Errors is a variadic pack of string literals representing possible error states.
 * 
 * example:
 *   using MyValueOrErrors = ValueOrErrors<int, "MyValue", "Error1", "Error2">;
 *   auto result = MyValueOrErrors(42);
 *   if (result) {
 *     int value = *result;
 *   } else if (result.is<"Error1">()) {
 *      // deal with error1
 *   } else if (result.is<"Error2">()) {
 *      // deal with error2
 *   }
 **/
template <typename T, StrLiteral OwnerMessage, StrLiteral... Errors>
struct ValueOrErrors {
  using value_type = T;

  static constexpr size_t error_count = sizeof...(Errors);
  static constexpr std::string_view owner = OwnerMessage.sv();
  static constexpr std::array<std::string_view, sizeof...(Errors)> error_names = {
      Errors.sv()...};

  /* Compile-time lookup of an error tag inside the pack. -1 if absent. */
  template <StrLiteral E> static constexpr int indexOf()
  {
    int idx = -1;
    int i = 0;
    (((E == Errors && idx < 0 ? (idx = i) : 0), ++i), ...);
    return idx;
  }

  template <StrLiteral E> static constexpr bool has_error = indexOf<E>() >= 0;

  /* --------------------------------------------------------------------- */

  static constexpr ValueOrErrors ok(value_type value)
  {
    return ValueOrErrors(std::move(value));
  }

  template <StrLiteral E> static constexpr ValueOrErrors error()
  {
    static_assert(has_error<E>, "error tag is not listed in this ValueOrErrors");
    return ValueOrErrors(ErrorIndex{indexOf<E>()});
  }

  /* In-place construction, avoids a move for expensive T. */
  template <typename... Args>
  static constexpr ValueOrErrors okInPlace(Args &&...args)
  {
    return ValueOrErrors(std::in_place, std::forward<Args>(args)...);
  }

  constexpr explicit ValueOrErrors(value_type value)
      : value_(std::move(value)), success_(true)
  {
  }

  template <typename... Args>
  constexpr explicit ValueOrErrors(std::in_place_t, Args &&...args)
      : value_(std::forward<Args>(args)...), success_(true)
  {
  }

  constexpr ValueOrErrors(ValueOrErrors &&other) noexcept(
      std::is_nothrow_move_constructible_v<T>)
      : empty_{}, success_(other.success_), errorIndex_(other.errorIndex_)
  {
    if (success_) {
      std::construct_at(std::addressof(value_), std::move(other.value_));
    }
  }

  constexpr ValueOrErrors &operator=(ValueOrErrors &&other) noexcept(
      std::is_nothrow_move_constructible_v<T>)
  {
    if (this == &other) {
      return *this;
    }
    reset();
    success_ = other.success_;
    errorIndex_ = other.errorIndex_;
    if (success_) {
      std::construct_at(std::addressof(value_), std::move(other.value_));
    }
    return *this;
  }

  ValueOrErrors(const ValueOrErrors &) = delete;
  ValueOrErrors &operator=(const ValueOrErrors &) = delete;

  constexpr ~ValueOrErrors()
    requires(std::is_trivially_destructible_v<T>)
  = default;

  constexpr ~ValueOrErrors()
  {
    reset();
  }

  /* --------------------------------------------------------------------- */

  constexpr bool success() const
  {
    return success_;
  }
  constexpr explicit operator bool() const
  {
    return success_;
  }

  /* Checked form: result.is<"ExpectedMethod">() */
  template <StrLiteral E> constexpr bool is() const
  {
    static_assert(has_error<E>, "error tag is not listed in this ValueOrErrors");
    return !success_ && errorIndex_ == indexOf<E>();
  }

  /* Value form, matching the original sketch: result.is(ExpectedMethod) */
  template <size_t N> constexpr bool is(const StrLiteral<N> &tag) const
  {
    return !success_ && errorIndex_ >= 0 && error_names[errorIndex_] == tag.sv();
  }

  constexpr std::string_view errorName() const
  {
    return success_ ? std::string_view{} : error_names[errorIndex_];
  }
  constexpr int errorIndex() const
  {
    return errorIndex_;
  }

  constexpr T &operator*() &
  {
    return value_;
  }
  constexpr const T &operator*() const &
  {
    return value_;
  }
  constexpr T &&operator*() &&
  {
    return std::move(value_);
  }

  constexpr T *operator->()
  {
    return std::addressof(value_);
  }
  constexpr const T *operator->() const
  {
    return std::addressof(value_);
  }

  template <typename U> constexpr T valueOr(U &&fallback) const &
  {
    return success_ ? value_ : static_cast<T>(std::forward<U>(fallback));
  }

private:
  struct ErrorIndex {
    int index;
  };

  constexpr explicit ValueOrErrors(ErrorIndex e)
      : empty_{}, success_(false), errorIndex_(e.index)
  {
  }

  constexpr void reset()
  {
    if (success_) {
      std::destroy_at(std::addressof(value_));
    }
    success_ = false;
    errorIndex_ = -1;
  }

  struct Empty {};

  union {
    Empty empty_;
    T value_;
  };
  bool success_ = false;
  int errorIndex_ = -1;
};
}

