#pragma once
#include "../util/vector.h"
#include "binding/binding_base.h"
#include "binding_literal.h"
#include "binding_struct.h"
#include "binding_types.h"
#include "concepts"
#include "type_traits"
#include <cstdint>

namespace litestl::binding {

template <std::same_as<bool> T> static types::Boolean *Bind()
{
  return new types::Boolean();
}

template <typename T>
  requires std::is_pointer_v<T> && (!std::same_as<T, void *>)
static types::Pointer *Bind()
{
  using P = std::remove_pointer_t<T>;
  return new types::Pointer(Bind<P>());
}

template <typename T>
  requires std::is_reference_v<T>
static types::Reference *Bind()
{
  using P = std::remove_reference_t<T>;
  return new types::Reference(Bind<P>());
}

} // namespace litestl::binding
