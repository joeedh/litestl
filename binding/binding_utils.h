#pragma once
#include "../util/vector.h"
#include "binding/binding_base.h"
#include "binding_bind.h"
#include "binding_literal.h"
#include "binding_struct.h"
#include "binding_types.h"
#include "concepts"
#include "type_traits"
#include <cstdint>

namespace litestl::binding {

template <> struct Binder<bool> {
  static types::Boolean *bind()
  {
    return new types::Boolean();
  }
};

// Binder<void *> (binding.h) is an explicit specialization, so it beats this
// partial specialization without needing an exclusion constraint.
template <typename T> struct Binder<T *> {
  static types::Pointer *bind()
  {
    return new types::Pointer(Bind<T>());
  }
};

template <typename T> struct Binder<T &> {
  static types::Reference *bind()
  {
    return new types::Reference(Bind<T>());
  }
};

} // namespace litestl::binding
