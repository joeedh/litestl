#pragma once

#include "binding_method_builder.h"

#include "binding_base.h"
#include "binding_constructor.h"
#include "binding_constructor_builder.h"
#include "binding_enum.h"
#include "binding_literal.h"
#include "binding_method.h"
#include "binding_method_builder.h"
#include "binding_number.h"
#include "binding_struct.h"

namespace litestl::binding {
// string binding
template <std::same_as<litestl::util::string> S> const BindingBase *Bind(S *)
{
  types::Struct<S> *st = new types::Struct<S>("litestl::util::String", sizeof(S));
  return st;
}
template <typename CLS>
concept ClassBindingReq = requires(types::Struct<CLS> *def) {
  { CLS::defineBindings() } -> std::convertible_to<const types::Struct<CLS> *>;
  std::is_reference_v<CLS> == false;
};

template <typename CLS>
const BindingBase *Bind(CLS *)
  requires ClassBindingReq<CLS>
{
  return CLS::defineBindings();
}

template <typename CLS>
const BindingBase *ClsBind()
  requires ClassBindingReq<CLS>
{
  return CLS::defineBindings();
}

} // namespace litestl::binding

#include "binding_template.h"
#include "binding_types.h"
#include "binding_utils.h"

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
template <IsVector VEC> const BindingBase *Bind(VEC *t)
{
  types::Struct<VEC> *st = new types::Struct<VEC>("litestl::util::Vector", sizeof(VEC));
  const BindingBase *bindT =
      Bind<typename VEC::value_type>((typename VEC::value_type *)nullptr);

  if (bindT->type == BindingType::Pointer) {
    // do not allow null for vector elements
    types::Pointer *p = static_cast<types::Pointer *>(
        static_cast<const types::Pointer *>(bindT)->clone());
    p->isNonNull = true;
    bindT = p;
  }

  st->addTemplateParam(bindT, "T");
  st->addTemplateParam(
      new types::NumLitType(VEC::staticSize, "static_size", Bind<int>(nullptr)),
      "static_size");

  return st;
}

} // namespace litestl::binding

#include "binding_union.h"
