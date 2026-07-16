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
 *
 * function_ref is trivially copyable, so wrapping one in a std::function stores
 * it inline in the small-object buffer rather than heap-allocating.
 */
template <typename function_base> class function_ref;
template <typename R, typename... Args> struct function_ref<R(Args...)> {
  /**
   * Constructs from any callable compatible with @p R(Args...). The same-type
   * exclusion keeps this forwarding constructor from shadowing the copy/move
   * constructors when the argument is another function_ref.
   */
  template <typename CallImpl,
            std::enable_if_t<
                !std::is_same_v<std::remove_cvref_t<CallImpl>, function_ref>,
                int> = 0>
  function_ref(CallImpl &&impl)
      : callback_(callback_impl<std::remove_reference_t<CallImpl>>),
        ptr_(reinterpret_cast<void *>(&impl))
  {
  }

  /* Defaulted (hence trivial) special members keep function_ref trivially
   * copyable — see the class doc comment. */
  function_ref(const function_ref &) = default;
  function_ref(function_ref &&) = default;
  function_ref &operator=(const function_ref &) = default;
  function_ref &operator=(function_ref &&) = default;

  /** Rebinds to any callable compatible with @p R(Args...). */
  template <typename CallImpl,
            std::enable_if_t<
                !std::is_same_v<std::remove_cvref_t<CallImpl>, function_ref>,
                int> = 0>
  function_ref &operator=(CallImpl &&impl)
  {
    callback_ = callback_impl<std::remove_reference_t<CallImpl>>;
    ptr_ = reinterpret_cast<void *>(&impl);
    return *this;
  }

  /** Invokes the referenced callable, forwarding @p args. */
  R operator()(Args... args) const
  {
    return callback_(ptr_, std::forward<Args>(args)...);
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
