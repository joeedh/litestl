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
  virtual size_t getSize() const override
  {
    return 0;
  }
  virtual BindingBase *clone()  const override
  {
    return static_cast<BindingBase *>(new Constructor(*this));
  }
};
} // namespace litestl::binding::types
