/** note: we keep bindings code out of the main math headers. */

#pragma once
#include "../binding/binding_bind.h"
#include "../binding/binding_constructor_builder.h"
#include "../binding/binding_struct.h"
#include "../binding/binding_template.h"
#include "../binding/binding_types.h"
#include "./aabb.h"
#include "./vector.h"
#include <type_traits>

namespace litestl::binding {

template <math::isMathVec T> struct Binder<T> {
  static BindingBase *bind()
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
    st->add("vec", 0, new types::Array(Bind<typename T::value_type>(), n));

    BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);
    BIND_STRUCT_COPY_CONSTRUCTOR(st);

    return st;
  }
};

template <math::isMathAABB T> struct Binder<T> {
  static types::Struct<T> *bind()
  {
    types::Struct<T> *st = new types::Struct<T>("litestl::math::AABB", sizeof(T));

    st->addTemplateParam(Bind<typename T::value_type>(), "T");

    types::ParentTemplateParam *p =
        new types::ParentTemplateParam("T", 0, Bind<typename T::value_type>());
    st->add("min", offsetof(T, min), p);
    st->add("max", offsetof(T, max), p);
    BIND_STRUCT_DEFAULT_CONSTRUCTOR(st);

    return st;
  }
};
} // namespace litestl::binding