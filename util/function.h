#pragma once

#include "compiler_util.h"
#include <functional>
#include <type_traits>
#include <utility>

namespace litestl::util {
using std::function;

#if 1

/**
 * Non-owning, lightweight reference to any callable with signature @p R(Args...).
 *
 * Unlike std::function, function_ref does not copy or heap-allocate the
 * callable. Instead it stores a type-erased void* pointer and a static
 * trampoline function pointer. The caller must ensure the referenced callable
 * outlives the function_ref.
 */
template <typename function_base> class function_ref;
template <typename R, typename... Args> struct function_ref<R(Args...)> {
  /** Constructs from any callable whose signature is compatible with @p R(Args...). */
  template <typename CallImpl>
  function_ref(CallImpl &&impl)
      : callback_(callback_impl<typename std::remove_reference_t<CallImpl>>)
  {
    ptr_ = reinterpret_cast<void *>(&impl);
  }
  function_ref(const function_ref &b) : callback_(b.callback_), ptr_(b.ptr_)
  {
  }
  function_ref(function_ref &&b) : callback_(b.callback_), ptr_(b.ptr_)
  {
    b.callback_ = nullptr;
    b.ptr_ = nullptr;
  }

  DEFAULT_MOVE_ASSIGNMENT(function_ref)

  function_ref &operator=(const function_ref &b)
  {
    if (&b == this) {
      return *this;
    }

    callback_ = b.callback_;
    ptr_ = b.ptr_;
    return *this;
  }

  template <typename CallImpl> function_ref &operator=(CallImpl &&impl)
  {
    callback_ = callback_impl<typename std::remove_reference_t<CallImpl>>;
    ptr_ = reinterpret_cast<void *>(&impl);
    return *this;
  }

  /** Copy constructor. */
  function_ref(const function_ref &b) : callback_(b.callback_), ptr_(b.ptr_)
  {
  }

  /** Move constructor. Nulls out the source. */
  function_ref(function_ref &&b) : callback_(b.callback_), ptr_(b.ptr_)
  {
    b.callback_ = nullptr;
    b.ptr_ = nullptr;
  }

  DEFAULT_MOVE_ASSIGNMENT(function_ref)

  /** Copy assignment. */
  function_ref &operator=(const function_ref &b)
  {
    if (&b == this) {
      return *this;
    }

    callback_ = b.callback_;
    ptr_ = b.ptr_;
    return *this;
  }

  /** Rebinds this function_ref to a new callable. */
  template <typename CallImpl> function_ref &operator=(CallImpl &&impl)
  {
    callback_ = callback_impl<typename std::remove_reference_t<CallImpl>>;
    ptr_ = reinterpret_cast<void *>(&impl);
    return *this;
  }

  /** Invokes the referenced callable, forwarding @p args. */
  template <typename... Args2> R operator()(Args2... args)
  {
    return callback_(ptr_, std::forward<Args2>(args)...);
  }

private:
  /** Static trampoline that casts @p ptr back to the concrete callable type and invokes
   * it. */
  template <typename CallImpl> static R callback_impl(void *ptr, Args... args)
  {
    return (*reinterpret_cast<CallImpl *>(ptr))(std::forward<Args>(args)...);
  }

  /** Function pointer to the type-erased trampoline. */
  R (*callback_)(void *ptr, Args... args);

  /** Type-erased pointer to the referenced callable. */
  void *ptr_;
};
#endif

} // namespace litestl::util
