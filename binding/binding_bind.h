#pragma once

#include "binding_base.h"

namespace litestl::binding {

/* Bind<T>() dispatches to the Binder<T>::bind() customization point.
 * The primary template is intentionally undefined: unbound types fail at
 * compile time; specializations may define bind() out-of-line in a .cc. */
template <typename T> struct Binder;

template <typename T> auto Bind()
{
  return Binder<T>::bind();
}

} // namespace litestl::binding
