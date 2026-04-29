#pragma once

#include "binding_base.h"
#include "binding_enum.h"
#include "binding_literal.h"
#include "binding_constructor.h"
#include "binding_method.h"
#include "binding_number.h"
#include "binding_struct.h"
#include "binding_types.h"
#include "binding_utils.h"
#include "binding_template.h"
#include "binding_constructor_builder.h"

#include "../math/math_bindings.h"

#include "generators/typescript.h"

// XXX vector binding utility, find where to put it other then this file!
namespace litestl::binding {
using litestl::util::Vector;

template <std::same_as<void *> T> const BindingBase *Bind()
{
  return new types::Number<unsigned char>(sizeof(void *) == 4 ? NumberType::Int32
                                                              : NumberType::Int64,
                                          "pointer",
                                          NumberFlags::Unsigned);
}

template <typename T>
concept IsVector = requires(T v) {
  // { Bind<T::value_type>() } -> std::derived_from<const BindingBase *>;
  std::same_as<typename T::is_litestl_vector, std::true_type>;
};

/** vector binding */
template <IsVector VEC> const BindingBase *Bind()
{
  types::Struct<VEC> *st = new types::Struct<VEC>("litestl::util::Vector", sizeof(VEC));
  st->addTemplateParam(Bind<typename VEC::value_type>(), "T");
  st->addTemplateParam(new types::NumLitType(VEC::staticSize, "static_size", Bind<int>()),
                       "static_size");

  return st;
}

// string binding
template <std::same_as<litestl::util::string> S> const BindingBase *Bind()
{
  types::Struct<S> *st = new types::Struct<S>("litestl::util::String", sizeof(S));
  return st;
}
} // namespace litestl::binding

#include "binding_union.h"
