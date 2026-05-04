#pragma once
#include "../util/vector.h"
#include "binding/binding_base.h"
#include "binding_literal.h"
#include "binding_struct.h"
#include "binding_types.h"
#include "concepts"
#include "type_traits"
#include <cstdint>
#include <type_traits>

namespace litestl::binding {

static types::Boolean *Bind(bool *)
{
  return new types::Boolean();
}


} // namespace litestl::binding
