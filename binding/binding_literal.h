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

struct NumLitType : public LiteralType {
  int data;

  NumLitType(int data, string name, BindingBase *bind)
      : LiteralType(LitType::Number, name, bind), data(data)
  {
  }
  NumLitType(const NumLitType &b) : LiteralType(b), data(b.data)
  {
  }

  virtual BindingBase *clone() const override
  {
    return static_cast<BindingBase *>(new NumLitType(*this));
  }

  virtual string buildFullName() const override
  {
    char buf[512];
    sprintf(buf, "%d", data);
    return string(buf);
  }
};

struct BoolLitType : public LiteralType {
  bool data;

  BoolLitType(bool data, string name, BindingBase *bind)
      : LiteralType(LitType::Bool, name, bind), data(data)
  {
  }
  BoolLitType(const BoolLitType &b) : LiteralType(b), data(b.data)
  {
  }

  virtual BindingBase *clone() const override
  {
    return static_cast<BindingBase *>(new BoolLitType(*this));
  }

  virtual string buildFullName() const override
  {
    return data ? "true" : "false";
  }
};

struct StrLitType : public LiteralType {
  string data;

  StrLitType(string data, string name)
      : LiteralType(LitType::String, name, nullptr), data(data)
  {
  }
  StrLitType(const StrLitType &b) : LiteralType(b), data(b.data)
  {
  }

  virtual BindingBase *clone() const override
  {
    return static_cast<BindingBase *>(new StrLitType(*this));
  }

  virtual string buildFullName() const override
  {
    return data;
  }
};

} // namespace litestl::binding::types