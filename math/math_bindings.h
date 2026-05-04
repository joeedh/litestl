/** note: we keep bindings code out of the main math headers. */

#pragma once
#include "../binding/binding_struct.h"
#include "../binding/binding_types.h"
#include "./aabb.h"
#include "./vector.h"
#include <type_traits>

namespace litestl::binding {

template <math::isMathVec T> static BindingBase *Bind(T *)
{
  int n = T::size;
  using value_type = typename T::value_type;
  string name = "unknown";

  if constexpr (std::is_same_v<value_type, float>) {
    name = "float";
  } else if constexpr (std::is_same_v<value_type, double>) {
    name = "double";
  } else if constexpr (std::is_same_v<value_type, int32_t>) {
    name = "int";
  } else if constexpr (std::is_same_v<value_type, int16_t>) {
    name = "short";
  } else if constexpr (std::is_same_v<value_type, int8_t>) {
    name = "byte";
  }

  // we have to do (n + '0') in this weird way to make clang happy
  char charN = (char)(n & 255) + '0';
  char size[2] = {charN, 0};
  name += string(size);

  types::Struct<T> *st = new types::Struct<T>("litestl::math::" + name, sizeof(T));
  st->add("vec", 0, new types::Array(Bind((typename T::value_type *)nullptr), n));
  return st;
}

template <math::isMathAABB T> //
static types::Struct<T> *Bind(T *)
{
  types::Struct<T> *st = new types::Struct<T>("litestl::math::AABB", sizeof(T));

  st->addTemplateParam(Bind((typename T::value_type *)nullptr), "T");
  BIND_STRUCT_MEMBER(st, min);
  BIND_STRUCT_MEMBER(st, max);
  BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);

  return st;
}
} // namespace litestl::binding