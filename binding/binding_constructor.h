#pragma once

#include "binding_base.h"
#include "binding_struct.h"
#include "binding_utils.h"
#include "util/vector.h"

#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

namespace litestl::binding::types {
using util::string;
using util::Vector;

struct ConstructorParam {
  string name;
  const BindingBase *type;
};

using ConstructorThunk = void (*)(void *outBuf, void **args);

struct Constructor : public BindingBase {
  const BindingBase *ownerType = nullptr;
  Vector<ConstructorParam> params;
  ConstructorThunk thunk = nullptr;

  Constructor(string name) : BindingBase(BindingType::Constructor, name)
  {
  }
  Constructor(const Constructor &b)
      : BindingBase(b), ownerType(b.ownerType), params(b.params), thunk(b.thunk)
  {
  }
  Constructor &argIsNullable(string argName)
  {
    for (auto &param : params) {
      if (param.name == argName && param.type->type == BindingType::Pointer) {
        // I suppose I could just const_cast here...
        Pointer *p = static_cast<Pointer *>(param.type->clone());
        delete param.type;
        p->isNonNull = false;
        param.type = p;
        return *this;
      } else if (param.name == argName) {
        printf("%s",
               std::format("arg {} not a pointer in method {}", argName, name).c_str());
        abort();
      }
    }

    printf("%s", std::format("arg {} not found in method {}", argName, name).c_str());
    abort();
  }
  virtual size_t getSize() const override
  {
    return 0;
  }
  virtual BindingBase *clone()  const override
  {
    return static_cast<BindingBase *>(new Constructor(*this));
  }
};

inline const Constructor *_StructBase::findConstructor(const string &name) const
{
  for (const Constructor *c : constructors) {
    if (c->name == name) {
      return c;
    }
  }
  return nullptr;
}
} // namespace litestl::binding::types
