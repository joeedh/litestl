#pragma once
#include "../util/vector.h"
#include "binding/binding_base.h"
#include "binding_literal.h"
#include "binding_types.h"
#include "binding_struct.h"
#include "concepts"
#include "type_traits"
#include <cstdint>

namespace litestl::binding {

template <std::same_as<bool> T> types::Boolean *Bind()
{
  return new types::Boolean();
}

} // namespace litestl::binding