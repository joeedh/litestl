#pragma once

#include "util/compiler_util.h"
#include "util/string.h"

namespace litestl::binding {
using util::string;

enum class BindingType {
  Boolean = 1 << 0,
  Number = 1 << 1,
  Pointer = 1 << 2,
  Reference = 1 << 3,
  Struct = 1 << 4,
  Array = 1 << 5,
};
FlagOperators(BindingType);

enum class NumberType {
  Int8 = 1 << 0,
  Int16 = 1 << 1,
  Int32 = 1 << 2,
  Int64 = 1 << 3,
  Float32 = 1 << 4,
  Float64 = 1 << 5,
};
FlagOperators(NumberType);
enum class NumberFlags {
  None = 0, //
  Unsigned = 1 << 0
};
FlagOperators(NumberFlags);

struct BindingBase {
  BindingType type;
  string name;

  BindingBase(BindingType type, string name) : type(type), name(name)
  {
    //
  }
  BindingBase(const BindingBase &b) : type(b.type), name(b.name)
  {
  }
  virtual BindingBase *clone()
  {
    return nullptr;
  }
  virtual size_t getSize()
  {
    return 0;
  }
};

} // namespace litestl::binding
