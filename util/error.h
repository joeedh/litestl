#pragma once

#include "compiler_util.h"
#include "string.h"
#include <cstdio>
#include <tuple>

namespace litestl::util {
template <StrLiteral typeName, StrLiteral msg> struct Error {
  static const StrLiteral message = msg;
  static const StrLiteral type = typeName;
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
  ValueOrError(const failed_type &c) : success(false)
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

} // namespace litestl::util
