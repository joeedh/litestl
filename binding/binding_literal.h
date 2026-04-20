#pragma once

#include "binding_base.h"
#include "litestl/util/string.h"

/** 
 * Literal binding types are types that map to typescript primitive literals,
 * e.g. litestl::util::StrLiteral<N> -> string, int/float/etc -> number, bool->boolean
 * These are emitted as actual primitive types, e.g.
 * export interface A {
 *   b: B<1, "string">;
 * }
 */
namespace litestl::binding::types {
enum class LitType { //
  Bool = 0,
  Number = 1,
  String = 2
};

struct LiteralType : public BindingBase {
  LitType litType;
  BindingBase *litBind;

  LiteralType(LitType litType, string name, BindingBase *litBind)
      : BindingBase(BindingType::Literal, name), litType(litType), litBind(litBind)
  {
  }
  LiteralType(const LiteralType &b)
      : BindingBase(b), litBind(b.litBind ? b.litBind->clone() : nullptr),
        litType(b.litType)
  {
  }
};

namespace detail {
template <typename T, LitType Type> struct LitTypeImpl : public LiteralType {
  T data;

  LitTypeImpl(T data, string name, BindingBase *bind)
      : LiteralType(Type, name, bind), data(data)
  {
  }
  LitTypeImpl(const LitTypeImpl &b) : LiteralType(b), data(b.data)
  {
  }

  virtual BindingBase *clone()
  {
    return static_cast<BindingBase *>(new LitTypeImpl(*this));
  }
};
} // namespace detail

using NumLitType = detail::LitTypeImpl<int32_t, LitType::Number>;
using BoolLitType = detail::LitTypeImpl<int32_t, LitType::Bool>;

struct StrLitType : public LiteralType {
  string data;

  StrLitType(string data, string name)
      : LiteralType(LitType::String, name, nullptr), data(data)
  {
  }
  StrLitType(const StrLitType &b) : LiteralType(b), data(b.data)
  {
  }

  virtual BindingBase *clone()
  {
    return static_cast<BindingBase *>(new StrLitType(*this));
  }
};

} // namespace litestl::binding::types